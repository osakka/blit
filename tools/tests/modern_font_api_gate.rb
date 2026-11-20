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
  %w[Blit-Terminus16.psf text/Hack-Regular.ttf text/3270-Regular.ttf BlitArabicModern.ttf NotoNaskhArabic-Regular.ttf NotoColorEmoji.ttf NotoEmojiText.ttf].each do |asset|
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
  # ADR 0241: naskh attaches the word face beside the companion; modern is the
  # companion alone, cell by cell, as before the setting existed.
  words = lambda do |active, label|
    arabic = active['arabic_companion']
    raise "#{label}: arabic is #{active['arabic'].inspect}, not naskh" unless active['arabic'] == 'naskh'
    raise "#{label}: word face not reported: #{arabic.inspect}" unless arabic.is_a?(Hash) &&
      arabic['shaping'] == 'words' && arabic['words_family'] == 'Noto Naskh Arabic' &&
      File.basename(arabic['words_path'].to_s) == 'NotoNaskhArabic-Regular.ttf'
  end
  cells = lambda do |active, label|
    arabic = active['arabic_companion']
    raise "#{label}: arabic is #{active['arabic'].inspect}, not modern" unless active['arabic'] == 'modern'
    raise "#{label}: modern still reports the word face: #{arabic.inspect}" unless arabic.is_a?(Hash) &&
      arabic['shaping'] == 'visual-cell-marks' && !arabic.key?('words_path') && !arabic.key?('words_family')
  end
  begin
    launch.call
    active = request.call('GET', '/v1/fonts').fetch('active')
    words.call(active, 'the default')
    raise 'a pixel font with the word face misreports the companion' unless active.dig('arabic_companion','path').nil? && active.dig('arabic_companion','family').nil?
    result = request.call('PUT', '/v1/fonts', {font_id:'hack', size_px:23})
    raise 'native face not selected' unless result.dig('active', 'rasterization') == 'native-antialiased'
    raise 'colour support absent' unless result.dig('active', 'colour_glyphs') == true
    raise 'Arabic companion absent' unless result.dig('active','arabic_companion','family') == 'Blit Arabic Modern'
    words.call(result['active'], 'hack')
    # arabic alone, either word, and back; the font and size stay.
    active = request.call('PUT', '/v1/fonts', {arabic:'modern'}).fetch('active')
    cells.call(active, 'arabic modern')
    raise 'arabic modern changed the font' unless active['id'] == 'hack' && active['size_px'] == 23
    raise 'modern lost the companion' unless active.dig('arabic_companion','family') == 'Blit Arabic Modern'
    renders = request.call('GET', '/v1/fonts').dig('active','raster_cache','renders')
    request.call('PUT', '/v1/fonts', {arabic:'modern'})
    raise 'an unchanged arabic rebuilt the raster' unless request.call('GET', '/v1/fonts').dig('active','raster_cache','renders') == renders
    words.call(request.call('PUT', '/v1/fonts', {arabic:'naskh', size_px:23}).fetch('active'), 'arabic naskh with a size')
    ['Naskh', 'MODERN', 'arabic', 'noto', '', 1, nil, true, ['naskh']].each do |bad|
      problem = request.call('PUT', '/v1/fonts', {arabic:bad}, 400)
      raise "arabic #{bad.inspect} -> #{problem.inspect}" unless problem['code'] == 'invalid_font_arabic' && problem['detail'].start_with?('arabic must be naskh or modern')
    end
    words.call(request.call('GET', '/v1/fonts').fetch('active'), 'after refusals')
    problem = request.call('PUT', '/v1/fonts', {}, 400)
    raise "empty font change -> #{problem.inspect}" unless problem['code'] == 'font_configuration_empty' && problem['detail'].include?('arabic')
    # The settings door stores and validates it.
    settings = settled.call
    raise "arabic not stored: #{settings.dig('document','font').inspect}" unless settings.dig('document','font','arabic') == 'naskh'
    document = settings.fetch('document')
    document['font']['arabic'] = 'kufi'
    problem = request.call('PUT', '/v1/settings', {document:document}, 400, {'If-Match'=>settings.fetch('revision').to_s})
    raise "settings arabic kufi -> #{problem.inspect}" unless problem['code'] == 'invalid_settings_font'
    document['font']['arabic'] = 'modern'
    request.call('PUT', '/v1/settings', {document:document}, 200, {'If-Match'=>settings.fetch('revision').to_s})
    cells.call(request.call('GET', '/v1/fonts').fetch('active'), 'settings arabic modern')
    # Survives a restart.
    settled.call
    stop.call; launch.call
    cells.call(request.call('GET', '/v1/fonts').fetch('active'), 'modern after a restart')
    request.call('PUT', '/v1/fonts', {arabic:'naskh'})
    # A document saved before the setting existed has no arabic: naskh.
    settled.call
    stop.call
    saved = JSON.parse(File.read(File.join(work, 'settings.json')))
    saved['font'].delete('arabic')
    File.write(File.join(work, 'settings.json'), JSON.generate(saved))
    launch.call
    active = request.call('GET', '/v1/fonts').fetch('active')
    words.call(active, 'a document without arabic')
    raise 'a document without arabic lost the font' unless active['id'] == 'hack' && active['size_px'] == 23
    %w[ibm-3270 hack].each do |profile|
      request.call('PUT', '/v1/fonts', {font_id:profile})
      (8..64).each do |size|
        active = request.call('PUT', '/v1/fonts', {size_px:size}).fetch('active')
        raise "incorrect #{profile} #{size}px metrics" unless active['size_px'] == size && active.dig('cell_pixels','height') == size && active['id'] == profile && active['rasterization'] == 'native-antialiased'
        raise 'wrong companion identity' unless File.basename(active.dig('arabic_companion','path')) == 'BlitArabicModern.ttf'
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
    raise 'bitmap misreports Arabic' unless active.dig('arabic_companion','path').nil? && active.dig('arabic_companion','family').nil?
    active = request.call('PUT', '/v1/fonts', {arabic:'modern'}).fetch('active')
    raise 'bitmap misreports Arabic' unless active['arabic'] == 'modern' && active['arabic_companion'].nil?
    words.call(request.call('PUT', '/v1/fonts', {arabic:'naskh'}).fetch('active'), 'blit-terminus16 back to naskh')
    active = request.call('PUT', '/v1/fonts', {font_id:'hack'}).fetch('active')
    raise 'modern policy changed' unless active['size_px'] == 23 && active['rasterization'] == 'native-antialiased'
    %w[hack ibm-3270].each do |profile|
      request.call('PUT', '/v1/fonts', {font_id:profile})
      ['NotoColorEmoji.ttf','NotoEmojiText.ttf','BlitArabicModern.ttf','NotoNaskhArabic-Regular.ttf'].each do |asset|
        path = File.join(fonts, asset)
        File.rename(path, path + '.missing')
        begin
          request.call('PUT', '/v1/fonts', {size_px:24}, 422)
          active = request.call('GET', '/v1/fonts').fetch('active')
          raise 'failed change destroyed active font' unless active['size_px'] == 23 && active['id'] == profile
          raise 'failed change lost loaded Arabic face' unless active['arabic_companion'].is_a?(Hash)
          words.call(active, "a failed change without #{asset}")
          if asset.start_with?('BlitArabic')
            inventory=request.call('GET','/v1/fonts')
            raise 'absent companion still available' if inventory['profiles'].find { |p| p['id']==profile }.fetch('available')
          end
          if asset.start_with?('NotoNaskh')
            # modern does not need the word face; naskh cannot come back
            # without it, and the refused change keeps modern as it was.
            cells.call(request.call('PUT', '/v1/fonts', {arabic:'modern'}).fetch('active'), "modern without #{asset}")
            request.call('PUT', '/v1/fonts', {size_px:24})
            request.call('PUT', '/v1/fonts', {size_px:23})
            request.call('PUT', '/v1/fonts', {arabic:'naskh'}, 422)
            active = request.call('GET', '/v1/fonts').fetch('active')
            cells.call(active, "a refused naskh without #{asset}")
            raise 'a refused naskh changed the font' unless active['size_px'] == 23 && active['id'] == profile
          end
        ensure
          File.rename(path + '.missing', path)
        end
      end
      words.call(request.call('PUT', '/v1/fonts', {arabic:'naskh'}).fetch('active'), "#{profile} naskh again")
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
    # A fonts root without the word face still starts (the default falls to
    # modern), but an explicit naskh is refused and changes nothing.
    settled.call
    stop.call
    saved = JSON.parse(File.read(File.join(work, 'settings.json')))
    saved['font'].delete('arabic')
    File.write(File.join(work, 'settings.json'), JSON.generate(saved))
    naskh = File.join(fonts, 'NotoNaskhArabic-Regular.ttf')
    File.rename(naskh, naskh + '.missing')
    begin
      launch.call
      active = request.call('GET', '/v1/fonts').fetch('active')
      cells.call(active, 'a boot without the word face')
      raise 'a boot without the word face lost the stored font' unless active['id'] == 'ibm-3270' && active['size_px'] == 23
      request.call('PUT', '/v1/fonts', {arabic:'naskh'}, 422)
      cells.call(request.call('GET', '/v1/fonts').fetch('active'), 'naskh refused without the word face')
      raise 'a refused naskh was stored' if request.call('GET', '/v1/settings').dig('document','font','arabic') == 'naskh'
    ensure
      File.rename(naskh + '.missing', naskh)
    end
    words.call(request.call('PUT', '/v1/fonts', {arabic:'naskh'}).fetch('active'), 'naskh once the face is back')
    puts 'MODERN_FONT_API_PASS sizes=57_each profiles=bitmap+modern+smooth_retro sizing=preserved save_restore=canonical missing_colour_or_text=rollback arabic=naskh+modern+stored+refused'
  rescue Exception
    warn File.read(File.join(work,'server.log'))
    raise
  ensure
    stop.call if pid
  end
end
