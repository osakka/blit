#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0094: hibr_compatibility follows the Hibr on disk. The owner upgraded
# Hibr from ABI 16 to 17 while Blit ran, and /v1/diagnostics/runtime kept
# reporting the replaced 0.99.427 as incompatible until Blit restarted. Here
# the configured Hibr is a stand-in that prints `hibr VERSION abi N`: it
# starts at ABI 16 and is replaced by rename, as dpkg does, with one that
# reports the pinned release and ABI. Within two check periods the report
# must be the new one, and compatible; a stand-in that is left alone must not
# be probed again (its call count stays put).

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(19596)
PINNED = File.read(File.join(ROOT, 'packaging/HIBR_VERSION')).strip
ABI = File.read(File.join(ROOT, 'packaging/HIBR_ABI')).strip.to_i

def fail!(why)
  raise "HIBR_IDENTITY_REFRESH_FAIL #{why}"
end

def compat
  body = Net::HTTP.get(URI("http://127.0.0.1:#{PORT}/v1/diagnostics/runtime"))
  JSON.parse(body).fetch('hibr_compatibility')
end

def stand_in(path, version, abi, calls)
  tmp = "#{path}.new"
  File.write(tmp, "#!/bin/sh\necho x >> #{calls}\necho 'hibr #{version} abi #{abi}'\n")
  FileUtils.chmod(0o755, tmp)
  File.rename(tmp, path)
end

Dir.mktmpdir('blit-hibr-identity') do |dir|
  hibr = File.join(dir, 'hibr')
  calls = File.join(dir, 'calls')
  stand_in(hibr, '0.99.1', ABI - 1, calls)
  env = gate_display_env(width: 320, height: 200).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_LOGIN_COMMAND' => hibr, 'BLIT_SETTINGS_PATH' => File.join(dir, 'settings.json'))
  log = File.join(dir, 'server.log')
  pid = gate_spawn(env, BINARY, PORT.to_s, out: log, err: [:child, :out])
  begin
    Timeout.timeout(15) { loop { break if (compat rescue nil); sleep 0.1 } }
    before = compat
    fail!("start: #{before}") unless before['runtime_abi'] == ABI - 1 &&
                                     before['runtime_version'] == '0.99.1' &&
                                     before['compatible'] == false
    # Left alone, it is not probed again.
    probes = File.readlines(calls).size
    sleep 11
    fail!("probed #{File.readlines(calls).size - probes} more times unchanged") unless
      File.readlines(calls).size == probes
    stand_in(hibr, PINNED, ABI, calls)
    after = nil
    begin
      Timeout.timeout(12) do
        loop do
          after = compat
          break if after['runtime_abi'] == ABI
          sleep 0.25
        end
      end
    rescue Timeout::Error
      fail!("still #{after} 12 s after the upgrade")
    end
    fail!("after: #{after}") unless after['runtime_version'] == PINNED &&
                                    after['compatible'] == true
    logs = JSON.parse(Net::HTTP.get(URI("http://127.0.0.1:#{PORT}/v1/diagnostics/runtime"))).fetch('logs')
    fail!("no runtime log entry for the change: #{logs.map { |l| l['code'] }}") unless
      logs.any? { |l| l['code'] == 'hibr_version_matches' }
    puts "HIBR_IDENTITY_REFRESH_PASS before=#{before['runtime_version']}/abi#{before['runtime_abi']} " \
         "after=#{after['runtime_version']}/abi#{after['runtime_abi']} compatible=true unchanged_not_reprobed=true"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
