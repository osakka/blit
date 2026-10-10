#!/usr/bin/env ruby
# frozen_string_literal: true
require 'json'; require 'net/http'; require 'uri'
BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
def call(m, p, b = nil)
  u = BASE + p
  k = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
        'DELETE' => Net::HTTP::Delete }.fetch(m)
  r = k.new(u); r['Authorization'] = "Bearer #{TOKEN}"
  if b then r['Content-Type'] = 'application/json'; r.body = JSON.generate(b) end
  res = Net::HTTP.start(u.hostname, u.port, read_timeout: 25) { |h| h.request(r) }
  [res.code.to_i, (res.body.to_s.empty? ? nil : (JSON.parse(res.body) rescue res.body))]
end
def rows(command, width, height)
  code, s = call('POST', '/v1/terminal-sessions',
                 { 'command' => '/bin/sh', 'arguments' => ['-c', "#{command}; sleep 0.3"],
                   'surface_mode' => 'terminal',
                   'surface' => { 'width' => width, 'height' => height } })
  abort "session -> #{code} #{s.inspect}" unless code == 201
  sleep 1.5
  _, d = call('GET', "/v1/surfaces/#{s.fetch('surface_id')}/cells")
  call('DELETE', "/v1/terminal-sessions/#{s.fetch('id')}")
  g = {}
  d.fetch('cells').each { |c| g[[c.fetch('x', 0), c.fetch('y', 0)]] = c['text'] || ' ' }
  (0...d.fetch('height')).map { |y| (0...d.fetch('width')).map { |x| g[[x, y]] || ' ' }.join }
end
failures = []
got = rows("printf 'AAAA1\\r\\nBBBB2\\r\\nCCCC3\\r\\nDDDD4'; printf '\\033[?69h\\033[3;6s\\033[1;3H\\033[2L'", 12, 5)
want = ['AA          ', 'BB          ', 'CCAA1       ', 'DDBB2       ', '  CC3       ']
puts "margin insert-line:"; got.each { |r| puts "  |#{r}|" }
failures << "insert-line: #{got.inspect}" unless got == want
got = rows("stty -icanon -echo min 0 time 5; printf '\\033P+q544e\\033\\\\'; dd if=/dev/tty bs=1 count=34 2>/dev/null | od -An -tx1 | tr -d ' \\n'", 110, 4)
tn = got.first.strip
puts "XTGETTCAP TN: #{tn}"
failures << "TN: #{tn}" unless tn == '1b50312b72353434453d373837343635373236443244364236393734373437391b5c'
if failures.empty?
  puts 'LIVE_TERMINAL_PASS margins=confined xtgettcap=TN'
else
  failures.each { |f| warn "live terminal: #{f}" }
  exit 1
end
