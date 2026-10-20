#!/usr/bin/env ruby
# Operator-only canary: exercise real systemd timer execution through the API.
# Does not restart Blit, alter its units/configuration, or touch Hibr.
require 'json'
require 'net/http'
require 'securerandom'
require 'timeout'

base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN')
request = lambda do |method, path, body = nil|
  req = {'GET'=>Net::HTTP::Get, 'POST'=>Net::HTTP::Post,
         'PATCH'=>Net::HTTP::Patch, 'DELETE'=>Net::HTTP::Delete}.fetch(method).new(base + path)
  req['Authorization'] = "Bearer #{token}"
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  res = Net::HTTP.start(base.host, base.port, open_timeout: 3, read_timeout: 10) { |http| http.request(req) }
  raise "#{method} #{path}: HTTP #{res.code} #{res.body}" unless res.code.start_with?('2')
  res.body.to_s.empty? ? nil : JSON.parse(res.body)
end
login = request.call('GET', '/v1/login')
raise 'log out before timer verification' if login.dig('identity', 'state') == 'authenticated'
original = request.call('GET', '/v1/terminal-sessions').fetch('sessions').find { |s| s['focused'] }
pane = request.call('GET', '/v1/topology').fetch('panes').find { |p| p['surface_id'].nil? }
raise 'requires a free pane; never replace an occupied desktop' unless pane
unit = "blit-timer-canary-#{SecureRandom.hex(6)}"
marker = "TIMER_CANARY_PASS_#{SecureRandom.hex(8)}"
script = <<~BASH
  set -eu
  [ "$(id -u)" = 0 ] || { echo TIMER_CANARY_REQUIRES_ROOT; exit 77; }
  canary_dir=$(mktemp -d /run/blit/timer-canary.XXXXXX)
  cleanup() {
    systemctl stop --no-block #{unit}.timer #{unit}.service >/dev/null 2>&1 || :
    rm -f "$canary_dir/fired"
    rmdir "$canary_dir"
  }
  trap cleanup EXIT
  systemd-run --quiet --collect --property=Type=exec --timer-property=AccuracySec=1s \
    --unit=#{unit} --on-active=2s /usr/bin/touch "$canary_dir/fired"
  for ((n=0; n<80; n++)); do
    if [ -f "$canary_dir/fired" ]; then echo #{marker}; exit 0; fi
    sleep 0.1
  done
  echo TIMER_CANARY_TIMEOUT
  exit 1
BASH
session = nil
begin
  session = request.call('POST', '/v1/terminal-sessions',
    # The API deliberately bounds each argument to255 bytes. Reassemble only
    # this operator-owned constant script, not any remotely supplied command.
    {command:'/bin/bash', arguments:['-c', 'eval "$(printf %s "$@")"', 'blit-timer-canary',
      *script.scan(/.{1,230}/m)], pane_id:pane.fetch('id')})
  request.call('PATCH', "/v1/terminal-sessions/#{original.fetch('id')}", {focused:true}) if original
  Timeout.timeout(15) do
    loop do
      cells = request.call('GET', "/v1/surfaces/#{session.fetch('surface_id')}/cells")
      text = cells.fetch('cells').sort_by { |c| [c['y'], c['x']] }.map { |c| c['text'] }.join
      break if text.include?(marker)
      observed = request.call('GET', "/v1/terminal-sessions/#{session.fetch('id')}")
      raise "canary exited without success: #{text}" if %w[stopped failed].include?(observed.fetch('state'))
      sleep 0.1
    end
  end
  # Success output precedes the shell's cleanup trap. Wait for cleanup/exit.
  Timeout.timeout(5) do
    loop do
      observed = request.call('GET', "/v1/terminal-sessions/#{session.fetch('id')}")
      raise 'canary cleanup failed' if observed.fetch('state') == 'failed'
      break if observed.fetch('state') == 'stopped'
      sleep 0.1
    end
  end
  puts 'HOST_PRESENTATION_TIMER_PASS actual_systemd=timer_and_service frontend_restart=none configuration_writes=none recovery=not_yet_proven'
ensure
  request.call('DELETE', "/v1/terminal-sessions/#{session.fetch('id')}") if session
  request.call('PATCH', "/v1/terminal-sessions/#{original.fetch('id')}", {focused:true}) if original
end
