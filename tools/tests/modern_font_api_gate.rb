#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'tmpdir'
require 'fileutils'
require 'timeout'
require_relative 'gate_port'
require_relative 'test_profile'

root = File.expand_path('../..', __dir__)
port = gate_port(19562)
base = URI("http://127.0.0.1:#{port}")
request = lambda do |method, path, body = nil, status = 200, headers = {}|
  q = {'GET'=>Net::HTTP::Get, 'PUT'=>Net::HTTP::Put, 'POST'=>Net::HTTP::Post}.fetch(method).new(base + path)
  q['Content-Type'] = 'application/json'
  headers.each { |k, v| q[k] = v }
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(base.host, base.port, read_timeout: 5) { |h| h.request(q) }
  raise "HTTP #{r.code}: #{r.body}" unless r.code.to_i == status
  JSON.parse(r.body)
end
Dir.mktmpdir('blit-modern-api') do |work|
  fonts = File.join(work, 'fonts'); Dir.mkdir(fonts); Dir.mkdir(File.join(fonts, 'text'))
  # ADR 0195: a fonts root holds the stack; text/ holds the text fonts.
  %w[Blit-Terminus16.psf text/Hack-Regular.ttf text/3270-Regular.ttf NotoNaskhArabic-Regular.ttf NotoColorEmoji.ttf NotoEmojiText.ttf].each do |asset|
    FileUtils.copy_file(File.join(root, 'assets/fonts', asset), File.join(fonts, asset))
  end
  env = TestProfile.environment.merge('BLIT_SETTINGS'=>'load',
    'BLIT_SETTINGS_PATH'=>File.join(work, 'settings.json'), 'BLIT_FONT_ROOT'=>fonts)
  pid = nil
  launch = lambda do
    pid = gate_spawn(env, TestProfile.binary, port.to_s, out:File.join(work, 'server.log'), err:[:child,:out])
    Timeout.timeout(10) do
      loop do
        begin
          request.call('GET', '/v1/version'); break
        rescue Errno::ECONNREFUSED
          sleep 0.02
        end
      end
    end
  end
  stop = lambda do
    Process.kill('TERM', pid)
    Timeout.timeout(10) { Process.wait(pid) }
    pid = nil
  end
  settled = lambda do
    Timeout.timeout(5) do
      loop do
        settings = request.call('GET', '/v1/settings')
        break settings if settings.dig('storage','state') == 'clean'
        sleep 0.05
      end
    end
  end
  # ADR 0241, 0243: one Arabic face, Noto Naskh Arabic's words, for every
  # text font, pixel fonts included; no companion, no choice.
  words = lambda do |active, label|
    arabic = active['arabic']
    raise "#{label}: the Arabic face is not reported: #{arabic.inspect}" unless arabic.is_a?(Hash) &&
      arabic.keys.sort == %w[family path shaping] && arabic['shaping'] == 'words' &&
      arabic['family'] == 'Noto Naskh Arabic' && File.basename(arabic['path'].to_s) == 'NotoNaskhArabic-Regular.ttf'
    raise "#{label}: a retired companion is reported" if active.key?('arabic_companion')
  end
  cells = lambda do |active, label|
    raise "#{label}: no word face, yet arabic is #{active['arabic'].inspect}" unless active.key?('arabic') && active['arabic'].nil?
  end
  retired = lambda do |inventory|
    raise 'a profile still names an Arabic companion' if inventory['profiles'].any? { |p| p.key?('arabic_companion_path') }
    raise 'the registry still holds an arabic_cells face' if inventory['registry'].any? { |f| f['role'] == 'arabic_cells' }
  end
  begin
    launch.call
    inventory = request.call('GET', '/v1/fonts')
    retired.call(inventory)
    words.call(inventory.fetch('active'), 'the default (a pixel font)')
    result = request.call('PUT', '/v1/fonts', {font_id:'hack', size_px:23})
    raise 'native face not selected' unless result.dig('active', 'rasterization') == 'native-antialiased'
    raise 'colour support absent' unless result.dig('active', 'colour_glyphs') == true
    words.call(result['active'], 'hack')
    # arabic is no longer a font setting: alone it is an empty change, and
    # beside a real one it is read past like any unknown key.
    ['naskh', 'modern', 'kufi', 1, nil].each do |old|
      problem = request.call('PUT', '/v1/fonts', {arabic:old}, 400)
      raise "arabic #{old.inspect} alone -> #{problem.inspect}" unless problem['code'] == 'font_configuration_empty' && !problem['detail'].include?('arabic')
    end
    renders = request.call('GET', '/v1/fonts').dig('active','raster_cache','renders')
    active = request.call('PUT', '/v1/fonts', {arabic:'modern', size_px:23}).fetch('active')
    words.call(active, 'arabic modern beside an unchanged size')
    raise 'an unchanged font rebuilt the raster' unless active['id'] == 'hack' && active['size_px'] == 23 && active.dig('raster_cache','renders') == renders
    # The settings door: the stored font has no arabic, and one sent back
    # by a client (from a 0.3.129 or 0.3.130 document) is read past.
    settings = settled.call
    raise "arabic stored: #{settings.dig('document','font').inspect}" if settings.dig('document','font').key?('arabic')
    document = settings.fetch('document')
    document['font']['arabic'] = 'kufi'
    request.call('PUT', '/v1/settings', {document:document}, 200, {'If-Match'=>settings.fetch('revision').to_s})
    words.call(request.call('GET', '/v1/fonts').fetch('active'), 'settings with an old arabic')
    # A document saved by 0.3.129 or 0.3.130, arabic modern, still loads:
    # its font, and Arabic as words.
    settled.call
    stop.call
    saved = JSON.parse(File.read(File.join(work, 'settings.json')))
    saved['font']['arabic'] = 'modern'
    File.write(File.join(work, 'settings.json'), JSON.generate(saved))
    launch.call
    active = request.call('GET', '/v1/fonts').fetch('active')
    words.call(active, 'a document with arabic modern')
    raise 'a document with arabic lost the font' unless active['id'] == 'hack' && active['size_px'] == 23
    # ... and the key is gone once the font is saved again.
    request.call('PUT', '/v1/fonts', {size_px:24})
    request.call('PUT', '/v1/fonts', {size_px:23})
    raise 'an old arabic was written back' if settled.call.dig('document','font').key?('arabic')
    %w[ibm-3270 hack].each do |profile|
      request.call('PUT', '/v1/fonts', {font_id:profile})
      (8..64).each do |size|
        active = request.call('PUT', '/v1/fonts', {size_px:size}).fetch('active')
        raise "incorrect #{profile} #{size}px metrics" unless active['size_px'] == size && active.dig('cell_pixels','height') == size && active['id'] == profile && active['rasterization'] == 'native-antialiased'
        words.call(active, "#{profile} #{size}px")
      end
    end
    request.call('PUT', '/v1/fonts', {size_px:23})
    [1.1, 1.7, 2.3, 3.0].each do |scale|
      active = request.call('PUT', '/v1/fonts', {scale:scale}).fetch('active')
      raise 'modern legacy scale policy failed' unless active['sizing'] == 'scale' && active['scale'] == scale && active['rasterization'] == 'native-antialiased'
    end
    request.call('PUT', '/v1/fonts', {size_px:23})
    active = request.call('PUT', '/v1/fonts', {font_id:'blit-terminus16'}).fetch('active')
    raise 'retro policy changed' unless active['size_px'] == 23 && active['rasterization'] == 'bitmap-nearest'
    words.call(active, 'blit-terminus16')
    active = request.call('PUT', '/v1/fonts', {font_id:'hack'}).fetch('active')
    raise 'modern policy changed' unless active['size_px'] == 23 && active['rasterization'] == 'native-antialiased'
    %w[hack ibm-3270].each do |profile|
      request.call('PUT', '/v1/fonts', {font_id:profile})
      ['NotoColorEmoji.ttf','NotoEmojiText.ttf','NotoNaskhArabic-Regular.ttf'].each do |asset|
        path = File.join(fonts, asset)
        File.rename(path, path + '.missing')
        begin
          # A change never loses a face the running raster has.
          request.call('PUT', '/v1/fonts', {size_px:24}, 422)
          active = request.call('GET', '/v1/fonts').fetch('active')
          raise 'failed change destroyed active font' unless active['size_px'] == 23 && active['id'] == profile
          words.call(active, "a failed change without #{asset}")
          inventory=request.call('GET','/v1/fonts')
          raise "#{profile} still available without #{asset}" if inventory['profiles'].find { |p| p['id']==profile }.fetch('available')
        ensure
          File.rename(path + '.missing', path)
        end
      end
      Timeout.timeout(5) do
        loop do
          settings = request.call('GET', '/v1/settings')
          break if settings.dig('storage','state') == 'clean'
          sleep 0.05
        end
      end
      font = request.call('GET', '/v1/settings').dig('document','font')
      raise 'competing saved size policies' unless font['font_id'] == profile && font['size_px'] == 23 && !font.key?('scale')
      stop.call; launch.call
      active = request.call('GET', '/v1/fonts').fetch('active')
      raise 'restart lost native font' unless active['id'] == profile && active['size_px'] == 23 && active['rasterization'] == 'native-antialiased'
      words.call(active, "#{profile} after a restart")
    end
    # A fonts root without the Arabic face still starts, with its stored
    # font: Arabic is drawn cell by cell from the glyph stack and
    # active.arabic is null. A change then goes ahead without the face, and
    # the first change after it is back attaches it.
    settled.call
    stop.call
    naskh = File.join(fonts, 'NotoNaskhArabic-Regular.ttf')
    File.rename(naskh, naskh + '.missing')
    begin
      launch.call
      active = request.call('GET', '/v1/fonts').fetch('active')
      cells.call(active, 'a boot without the word face')
      raise 'a boot without the word face lost the stored font' unless active['id'] == 'ibm-3270' && active['size_px'] == 23
      active = request.call('PUT', '/v1/fonts', {size_px:24}).fetch('active')
      cells.call(active, 'a change without the word face')
      raise 'a change without the word face was refused' unless active['size_px'] == 24
    ensure
      File.rename(naskh + '.missing', naskh)
    end
    words.call(request.call('PUT', '/v1/fonts', {size_px:23}).fetch('active'), 'a change once the face is back')
    puts 'MODERN_FONT_API_PASS sizes=57_each profiles=bitmap+modern+smooth_retro sizing=preserved save_restore=canonical missing_colour_or_text=rollback arabic=one_face+old_setting_read_past+boot_without_face'
  rescue Exception
    warn File.read(File.join(work,'server.log'))
    raise
  ensure
    stop.call if pid
  end
end
