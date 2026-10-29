#!/usr/bin/env ruby
# Build a real shared-raster specimen; no SDL, display, Hibr or host-font input.
require 'tmpdir'
require 'zlib'
require 'fileutils'
require 'json'
root = File.expand_path('../..', __dir__)
arabic = ARGV.delete('--arabic')
output = File.expand_path(ARGV.fetch(0, arabic ? 'build/font-preview/blit-arabic.png' : 'build/font-preview/blit-fonts.png'))
Dir.chdir(root) do
  Dir.mktmpdir('blit-font-preview') do |work|
    binary = File.join(work, 'preview')
    headers = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")
    library = ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall', "-I#{headers}",
      arabic ? 'tools/probes/arabic_preview.c' : 'tools/probes/font_preview.c', 'src/blit_font.c', 'src/blit_raster.c', library,
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-o', binary)
    ppm = File.join(work, 'specimen.ppm')
    if arabic
      arguments = ['assets/fonts/Blit-Terminus16.psf',
        'assets/fonts/text/3270-Regular.ttf', 'assets/fonts/text/Hack-Regular.ttf']
      samples = JSON.parse(File.read('assets/fonts/arabic-specimens.json')).fetch('samples')
      input = File.join(work, 'samples.txt')
      File.write(input, samples.map { |s| cp=s.fetch('visual').codepoints; [cp.size,*cp.map { |c| c.to_s(16) }].join(' ') }.join("\n"))
      arguments.concat(['assets/fonts/BlitArabicModern.ttf', input, ppm])
    else
      # Icon rows from the catalogue, so a renamed or retired icon fails here
      # instead of drawing nothing. Two-column icons are "left,right".
      by_id = JSON.parse(File.read('assets/fonts/icons/catalogue.json')).fetch('icons')
                  .to_h { |i| [i.fetch('id'), i.fetch('codepoint')] }
      icon = lambda do |id|
        next by_id.fetch(id) if by_id.key?(id)
        "#{by_id.fetch("#{id}-left")},#{by_id.fetch("#{id}-right")}"
      end
      file = ->(kind, colour = 'classic') { icon.call("file-#{kind}-#{colour}") }
      rows = [
        %w[blit hibr-pen wifi-1 wifi-2 wifi-3 wifi-4 bluetooth battery-0 battery-2 battery-4
           battery-2-charging battery-ac volume-0 volume-1 volume-3 power].map(&icon),
        %w[phone-off phone-wait phone-on phone-ring phone-classic-off phone-classic-wait
           phone-classic-on phone-classic-ring].map(&icon) + [icon.call('hui')],
        %w[classic blue green amber].map { |c| file.call('folder', c) } +
          %w[folder-git folder-downloads folder-music folder-games home share].map { |k| file.call(k) },
        %w[document text pdf image spreadsheet presentation markdown code terminal archive].map { |k| file.call(k) },
        %w[audio video boxer rom ebook usb key database c trash].map { |k| file.call(k) } +
          %w[jpg png].map { |f| icon.call("file-image-#{f}") }
      ]
      rows_file = File.join(work, 'icons.txt')
      File.write(rows_file, rows.map { |r| r.join(' ') }.join("\n") + "\n")
      names = { 'Hack-Regular.ttf' => 'Hack', 'JetBrainsMonoNL-Regular.ttf' => 'JetBrains Mono',
                'FiraCode-Regular.ttf' => 'Fira Code', 'IBMPlexMono-Regular.ttf' => 'IBM Plex Mono',
                '3270-Regular.ttf' => 'IBM 3270' }
      faces = JSON.parse(File.read('assets/fonts/fonts.source.json')).fetch('text').map { |t| t.fetch('file') }
      arguments = ['assets/fonts/Blit-Terminus16.psf', 'assets/fonts/NotoColorEmoji.ttf', rows_file, ppm] +
        faces.map { |f| "#{names.fetch(File.basename(f))}=assets/fonts/#{f}" }
    end
    raise 'raster failed' unless system(binary, *arguments)
    bytes = File.binread(ppm)
    header = /\AP6\n(\d+) (\d+)\n255\n/.match(bytes) or raise 'invalid specimen'
    w, h = header.captures.map(&:to_i)
    rgb = bytes.byteslice(header[0].bytesize..)
    raise 'truncated specimen' unless rgb.bytesize == w * h * 3
    chunk = ->(type, data) { [data.bytesize].pack('N') + type + data + [Zlib.crc32(type + data)].pack('N') }
    scanlines = (0...h).map { |y| "\0".b + rgb.byteslice(y * w * 3, w * 3) }.join
    png = "\x89PNG\r\n\x1a\n".b + chunk.call('IHDR', [w, h, 8, 2, 0, 0, 0].pack('NNC5')) +
      chunk.call('IDAT', Zlib.deflate(scanlines, 9)) + chunk.call('IEND', ''.b)
    FileUtils.mkdir_p(File.dirname(output))
    File.binwrite(output, png)
    puts "FONT_PREVIEW #{output} #{w}x#{h} source=production_raster"
  end
end
