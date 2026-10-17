#!/usr/bin/env ruby
# Real PTY reproduction of #94; uses only a free pane and removes its session.
require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
def req(method, path, body = nil)
  uri = BASE + path
  q = {'GET'=>Net::HTTP::Get, 'POST'=>Net::HTTP::Post,
       'PATCH'=>Net::HTTP::Patch, 'DELETE'=>Net::HTTP::Delete}.fetch(method).new(uri)
  q['Authorization'] = "Bearer #{TOKEN}"
  q['Content-Type'] = 'application/json' if body
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(uri.host, uri.port, open_timeout: 5, read_timeout: 30) { |h| h.request(q) }
  raise "#{method} #{path}: #{r.code}" unless r.code.start_with?('2')
  r.body.nil? || r.body.empty? ? nil : JSON.parse(r.body)
end

topology = req('GET', '/v1/topology')
pane = topology.fetch('panes').find { |p| p['surface_id'].nil? }
raise 'requires a free pane' unless pane
raise 'requires 80x40 cells' unless pane.dig('cell_grid', 'columns') >= 80 && pane.dig('cell_grid', 'rows') >= 40
focused = req('GET', '/v1/terminal-sessions').fetch('sessions').find { |s| s['focused'] }
session = nil
begin
  # A complete shell builtin printf emits the continuation plus >4 KiB of
  # ANSI/text. The delay forces the preceding UTF-8 lead into an earlier read.
  script = 'p=; i=0; while [ "$i" -lt 1000 ]; do p="${p}xxxx"; i=$((i+1)); done; ' \
           'printf "\342"; sleep 0.2; printf "\226\200%s\033[31;70HEND" "$p"; sleep 30'
  session = req('POST', '/v1/terminal-sessions',
    {'command'=>'/bin/sh', 'arguments'=>['-c', script], 'pane_id'=>pane['id']})
  req('PATCH', "/v1/terminal-sessions/#{focused['id']}", {'focused'=>true}) if focused
  state = nil
  40.times do
    state = req('GET', "/v1/surfaces/#{session['surface_id']}/cells")
    break if state.fetch('cells').any? { |c| c['x'] == 71 && c['y'] == 30 && c['text'] == 'D' }
    sleep 0.05
  end
  cells = state.fetch('cells').to_h { |c| [[c['x'], c['y']], c['text']] }
  marker = (69..71).map { |x| cells[[x,30]] || ' ' }.join
  blocks = cells.values.count("\u2580")
  text = cells.values.count('x')
  evidence = ENV['BLIT_EVIDENCE_DIR']
  if evidence
    Dir.mkdir(evidence) unless Dir.exist?(evidence)
    File.write(File.join(evidence, 'utf8-pty-state.json'), JSON.generate(state))
  end
  raise "LIVE_UTF8_PTY_FAIL half_blocks=#{blocks} text_cells=#{text}/4000 marker=#{marker.inspect}" unless blocks == 1 && text == 4000 && marker == 'END'
  puts "LIVE_UTF8_PTY_PASS half_blocks=#{blocks} text_cells=#{text} marker=#{marker} revision=#{state['revision']}"
ensure
  req('DELETE', "/v1/terminal-sessions/#{session['id']}") if session
  req('PATCH', "/v1/terminal-sessions/#{focused['id']}", {'focused'=>true}) if focused
end
