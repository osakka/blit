#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require_relative 'gate_port'
require_relative 'test_profile'
port = gate_port(19567)
base = URI("http://127.0.0.1:#{port}")
request = lambda do |method, path, body = nil, expected = 200|
  q = {'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,'DELETE'=>Net::HTTP::Delete,'PATCH'=>Net::HTTP::Patch}.fetch(method).new(base + path)
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(base.host, base.port, read_timeout:5) { |h| h.request(q) }
  raise "#{method} #{path}: #{r.code} #{r.body}" unless r.code.to_i == expected
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
Dir.mktmpdir('blit-emoji-pty') do |work|
  env = TestProfile.environment.merge('BLIT_FONT_ROOT'=>File.join(TestProfile::ROOT,'assets/fonts'))
  pid = spawn(env, TestProfile.binary, port.to_s,
    out:File.join(work,'server.log'),err:[:child,:out])
  begin
    Timeout.timeout(8) do
      loop do
        begin
          request.call('GET','/v1/version'); break
        rescue Errno::ECONNREFUSED
          sleep 0.02
        end
      end
    end
    request.call('PUT','/v1/fonts',{font_id:'blit-modern',size_px:16})
    display = TestProfile.display_id!(request.call('GET','/v1/displays'))
    capture = lambda do
      r = Net::HTTP.get_response(base + "/v1/diagnostics/screenshots/#{display}")
      raise 'capture failed' unless r.code == '200' && r.body.start_with?('BM')
      r.body
    end
    samples = ["❤️", "1️⃣", "🇬🇧", "👍🏽", "👩‍💻", "👨‍👩‍👧‍👦", "🏴\u{e0067 e0062 e0065 e006e e0067 e007f}", "\u2764\ufe0e", "\u{1f41f}\ufe0e"]
    # Independent scalar oracle from the canonical width data, not a second
    # production width engine. Byte writes need not equal OS read boundaries.
    ranges = File.read(File.join(TestProfile::ROOT,'src/blit_unicode_width.h'))
      .scan(/\{\s*0x([0-9a-f]+),\s*0x([0-9a-f]+),\s*(\d)\s*\}/i)
      .map { |a,b,w| [a.to_i(16),b.to_i(16),w.to_i] }
    scalar_width = ->(cp) { ranges.find { |a,b,_| cp >= a && cp <= b }&.last || 1 }
    [nil, 'scalar', 'emoji'].each do |policy|
      [false,true].each do |fragmented|
        samples.each do |sequence|
          program = fragmented ? 'STDOUT.binmode; STDOUT.sync=true; ARGV[0].bytes.each{|b|STDOUT.write(b.chr);sleep 0.002};sleep 5' : 'STDOUT.binmode; STDOUT.sync=true; STDOUT.write(ARGV[0]);sleep 5'
          body = {command:RbConfig.ruby,arguments:['-e',program,"\e[?25l"+sequence+'X'],surface:{width:16,height:2},
                  environment:{'text_layout'=>(policy == 'emoji' ? 'scalar' : 'emoji')}}
          body[:text_layout] = policy if policy
          session = request.call('POST','/v1/terminal-sessions',body,201)
          path = "/v1/terminal-sessions/#{session.fetch('id')}"
          begin
            raise 'reported session policy wrong' unless session['text_layout'] == (policy || 'scalar')
            snapshot = Timeout.timeout(5) do
              loop do
                value = request.call('GET',"/v1/surfaces/#{session.fetch('surface_id')}/cells")
                break value if value.fetch('cells').any? { |c| c['text'] == 'X' }
                sleep 0.01
              end
            end
            cells = snapshot.fetch('cells').select { |c| c['y'] == 0 }.to_h { |c| [c['x'],c] }
            scalar_columns = sequence.codepoints.sum { |cp| scalar_width.call(cp) }
            expected_x = policy == 'emoji' && !sequence.end_with?("\ufe0e") ? 2 : scalar_columns
            raise 'PTY cursor/advance wrong' unless cells.fetch(expected_x)['text'] == 'X' && snapshot.dig('cursor','x') == expected_x + 1
            observed = cells.select { |x,c| x <= expected_x && c['width'] != 0 }.sort.map { |_,c| c['text'] }.join
            raise "PTY lost/reordered scalars policy=#{policy.inspect} text=#{observed.inspect}" unless observed == sequence + 'X'
            if policy == 'emoji'
              raise 'PTY split cluster ownership' unless cells[0]['text'] == sequence && cells[0]['width'] == expected_x && (expected_x == 1 || cells[1]['width'] == 0)
              pty_pixels = capture.call
              surface_path = "/v1/surfaces/#{session.fetch('surface_id')}/cells"
              current = request.call('GET',surface_path)
              request.call('PUT',surface_path,{base_revision:current['revision'],text_layout:'emoji',
                cells:[{x:0,y:0,text:' '*16},{x:0,y:0,text:sequence+'X'}]})
              raise 'PTY and positioned API pixels differ' unless capture.call == pty_pixels
            end
            before = request.call('GET',path)
            error = request.call('PATCH',path,{text_layout:'emoji'},400)
            raise 'mutable policy accepted' unless error['code'] == 'immutable_text_layout' && request.call('GET',path)['text_layout'] == before['text_layout']
          ensure
            request.call('DELETE',path,nil,204)
          end
        end
      end
    end
    [nil,true,0,[],{},'', 'bad'].each do |policy|
      before = request.call('GET','/v1/terminal-sessions')
      error = request.call('POST','/v1/terminal-sessions',{command:'/bin/cat',text_layout:policy},400)
      raise 'invalid policy not diagnosed before creation' unless error['code'] == 'invalid_text_layout' && request.call('GET','/v1/terminal-sessions') == before
    end
    puts "EMOJI_PTY_API_PASS sequences=#{samples.size} policies=default+scalar+emoji writes=contiguous+bytewise cases=#{samples.size * 6} ownership=exact fresh_capture_parity=#{samples.size * 2} immutable=400 invalid_create=atomic scope=real_local_PTY_not_physical"
  ensure
    Process.kill('TERM',pid) rescue nil
    Timeout.timeout(8) { Process.wait(pid) }
  end
end
