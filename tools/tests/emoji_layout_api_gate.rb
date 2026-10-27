#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'test_profile'
port = gate_port(19566)
base = URI("http://127.0.0.1:#{port}")
request = lambda do |method, path, body = nil, expected = 200|
  q = {'GET'=>Net::HTTP::Get, 'POST'=>Net::HTTP::Post, 'PUT'=>Net::HTTP::Put}.fetch(method).new(base + path)
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(base.host, base.port, read_timeout:5) { |h| h.request(q) }
  raise "#{method} #{path}: #{r.code} #{r.body}" unless r.code.to_i == expected
  JSON.parse(r.body)
end
Dir.mktmpdir('blit-emoji-api') do |work|
  env = TestProfile.environment.merge('BLIT_FONT_ROOT'=>File.join(TestProfile::ROOT,'assets/fonts'))
  pid = spawn(env, TestProfile.binary, port.to_s,
              out:File.join(work,'server.log'), err:[:child,:out])
  begin
    Timeout.timeout(8) do
      loop do
        begin
          request.call('GET', '/v1/version'); break
        rescue Errno::ECONNREFUSED
          sleep 0.02
        end
      end
    end
    request.call('PUT', '/v1/fonts', {font_id:'hack',size_px:16})
    session = request.call('POST', '/v1/terminal-sessions', {command:'/bin/cat', surface:{width:16,height:2}}, 201)
    path = "/v1/surfaces/#{session.fetch('surface_id')}/cells"
    samples = ["❤️", "1️⃣", "🇬🇧", "👍🏽", "👩‍💻", "👨‍👩‍👧‍👦", "🏴\u{e0067 e0062 e0065 e006e e0067 e007f}"]
    samples.each do |sequence|
      before = request.call('GET', path)
      request.call('PUT', path, {base_revision:before.fetch('revision'), text_layout:'emoji',
        cells:[{x:0,y:0,text:' ' * 16},{x:0,y:0,text:sequence + 'X'}]})
      cells = request.call('GET', path).fetch('cells').select { |c| c['y'] == 0 }.to_h { |c| [c['x'],c] }
      raise 'sequence not owned by one wide cell' unless cells[0]['text'] == sequence && cells[0]['width'] == 2 && cells[1]['width'] == 0 && cells[2]['text'] == 'X'
      revision = request.call('GET', path).fetch('revision')
      request.call('PUT', path, {base_revision:revision, cells:[{x:1,y:0,text:'A'}]})
      cells = request.call('GET', path).fetch('cells').select { |c| c['y'] == 0 }.to_h { |c| [c['x'],c] }
      raise 'continuation overwrite left stale anchor' unless (!cells[0] || cells[0]['text'] == ' ') && cells[1]['text'] == 'A'
    end
    legacy = []
    before = request.call('GET', path)
    request.call('PUT', path, {base_revision:before['revision'], text_layout:'emoji', cells:[{x:before.fetch('width') - 1,y:0,text:samples[0]}]})
    raise 'right-edge cluster was split' unless request.call('GET', path).fetch('cells') == before.fetch('cells')
    [nil, 'scalar'].each do |policy|
      before = request.call('GET', path)
      body = {base_revision:before['revision'], cells:[{x:0,y:0,text:' ' * 16},{x:0,y:0,text:"👩‍💻X"}]}
      body[:text_layout] = policy if policy
      request.call('PUT', path, body)
      legacy << request.call('GET', path).fetch('cells')
    end
    raise 'default scalar compatibility changed' unless legacy[0] == legacy[1] && legacy[0].any? { |c| c['x'] == 4 && c['y'] == 0 && c['text'] == 'X' }
    [nil, true, 0, [], {}, '', 'bad'].each do |policy|
      before = request.call('GET', path)
      error = request.call('PUT', path, {base_revision:before['revision'], text_layout:policy, cells:[{x:0,y:0,text:'Z'}]}, 400)
      raise 'invalid policy not diagnosed' unless error['code'] == 'invalid_text_layout'
      raise 'invalid policy changed surface' unless request.call('GET', path) == before
    end
    puts 'EMOJI_LAYOUT_API_PASS sequences=7 wide_ownership=exact continuation_overwrite=clean scalar_default=unchanged invalid_policy=atomic scope=API_not_PTY'
  ensure
    Process.kill('TERM', pid) rescue nil
    Timeout.timeout(8) { Process.wait(pid) }
  end
end
