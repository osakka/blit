#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0189: settings saves never block the main loop, and the receipt is
# unchanged. Every write is made slow on purpose (BLIT_SETTINGS_WRITE_DELAY_MS,
# a gate-only fault injection standing in for a disk under writeback, as on
# 2026-10-26 when an fsync held the loop for nine seconds and the console
# watchdog killed Blit).
#
#   * PUT /v1/settings answers once its change is durable: about the delay
#     later, 200, with a clean receipt
#   * meanwhile the loop runs: GET /v1/version answers quickly and a drawing
#     pane keeps being presented
#   * meanwhile GET /v1/settings says pending, with the old saved revision
#   * a second PUT inside the window waits too, and both are answered
#   * DELETE waits for its removal, then resets the live settings
#   * a write that fails answers its waiting PUT 503, as before

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19537'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
DELAY_MS = 3000

def request(method, path, body = nil, revision = nil, timeout: 40)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['If-Match'] = revision.to_s if revision
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: timeout) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code}" unless code == 200
  value
end

def check(ok, why)
  raise "SETTINGS_WRITER_FAIL #{why}" unless ok
end

def clock
  Process.clock_gettime(Process::CLOCK_MONOTONIC)
end

Dir.mktmpdir('blit-settings-writer') do |root|
  store = File.join(root, 'store')
  Dir.mkdir(store)
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(store, 'settings.json'),
    'BLIT_SETTINGS_WRITE_DELAY_MS' => DELAY_MS.to_s)
  log = File.join(root, 'server.log')
  pid = spawn(env, BINARY, PORT.to_s, out: log, err: [:child, :out])
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    # A pane that draws all the time, so presents can be counted.
    code, = request('POST', '/v1/terminal-sessions',
                    { command: '/bin/sh', arguments: ['-c', 'i=0; while :; do i=$((i + 1)); printf "\\r%d " $i; sleep 0.02; done'],
                      display_id: 'display-1' })
    check(code == 201, "drawing session -> #{code}")
    # Settle: any startup save has been made.
    Timeout.timeout(20) { sleep 0.1 until get('/v1/settings').dig('storage', 'state') == 'clean' }

    put = lambda do |interval|
      settings = get('/v1/settings')
      document = settings['document']
      document['server'] = (document['server'] || {}).merge('render_interval_ms' => interval)
      started = clock
      code, reply = request('PUT', '/v1/settings', { document: document }, settings['revision'])
      [code, reply, clock - started]
    end

    # One slow save: the PUT waits for it; the loop does not.
    before = get('/v1/settings')
    presents_before = get('/v1/diagnostics').dig('render', 'outputs', 0, 'present_calls')
    result = nil
    writer = Thread.new { result = put.call(11) }
    sleep 0.5
    worst = 0.0
    pending_seen = false
    while writer.alive?
      started = clock
      code, = request('GET', '/v1/version')
      worst = [worst, clock - started].max
      check(code == 200, "version during a slow save -> #{code}")
      settings = get('/v1/settings')
      if settings.dig('storage', 'state') == 'pending'
        pending_seen = true
        check(settings.dig('storage', 'saved_revision') == before.dig('storage', 'saved_revision'),
              "saved revision moved before the save was durable: #{settings['storage']}")
      end
      sleep 0.2
    end
    writer.join
    code, reply, took = result
    check(code == 200 && reply.dig('storage', 'state') == 'clean',
          "slow save receipt: #{code} #{reply.is_a?(Hash) ? reply['storage'] : reply}")
    check(took >= DELAY_MS / 1000.0 * 0.9, "PUT answered before its save was durable: #{took.round(2)} s")
    check(worst < 0.25, "the loop waited on the disk: GET /v1/version took #{(worst * 1000).round} ms")
    check(pending_seen, 'storage never reported pending during the save')
    presents_after = get('/v1/diagnostics').dig('render', 'outputs', 0, 'present_calls')
    check(presents_after - presents_before >= 20,
          "the drawing pane was not presented during the save: #{presents_before} -> #{presents_after}")
    check(get('/v1/settings').dig('effective', 'render_interval_ms') == 11, 'the saved interval did not apply')

    # Two saves in one window: the second waits for its own write; both answer.
    first = nil
    second = nil
    t1 = Thread.new { first = put.call(12) }
    sleep 0.5
    t2 = Thread.new { second = put.call(13) }
    [t1, t2].each(&:join)
    check(first[0] == 200 && second[0] == 200, "overlapping saves: #{first[0]} #{second[0]}")
    check(second[1].dig('storage', 'state') == 'clean', "the later save's receipt: #{second[1]['storage']}")
    check(second[2] > first[2] - 1.0, 'the later save did not wait for its own write')
    check(get('/v1/settings').dig('effective', 'render_interval_ms') == 13, 'the later save did not win')
    saved = JSON.parse(File.read(File.join(store, 'settings.json')))
    check(saved.dig('server', 'render_interval_ms') == 13, "disk holds #{saved['server']}")

    # DELETE waits for the removal, then resets the live settings.
    settings = get('/v1/settings')
    started = clock
    code, reply = request('DELETE', '/v1/settings', nil, settings['revision'])
    took = clock - started
    check(code == 200 && took >= DELAY_MS / 1000.0 * 0.9, "DELETE: #{code} after #{took.round(2)} s")
    check(reply.dig('effective', 'render_interval_ms') != 13, "DELETE did not reset: #{reply['effective']}")
    check(!File.exist?(File.join(store, 'settings.json')), 'DELETE left the file')

    # A write that fails answers its waiting PUT 503, as before.
    Timeout.timeout(20) { sleep 0.1 until get('/v1/settings').dig('storage', 'state') != 'pending' }
    File.chmod(0o555, store)
    code, reply, = put.call(14)
    File.chmod(0o755, store)
    check(code == 503 && reply['code'] == 'settings_write_failed', "failed save -> #{code} #{reply}")
    check(get('/v1/settings').dig('storage', 'state') == 'error', 'a failed save is not reported')
    # And it recovers once the disk does.
    Timeout.timeout(20) { sleep 0.2 until get('/v1/settings').dig('storage', 'state') == 'clean' }

    puts "SETTINGS_WRITER_PASS delay_ms=#{DELAY_MS} put_waits=durable loop_worst_ms=#{(worst * 1000).round} " \
         "presents_during=#{presents_after - presents_before} overlap=both_answered delete=after_removal failure=503"
  ensure
    File.chmod(0o755, store) rescue nil
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
