#!/usr/bin/env ruby
# Build a real shared-raster specimen; no SDL, display, Hibr or host-font input.
require 'tmpdir'
require 'zlib'
require 'fileutils'
root = File.expand_path('../..', __dir__)
output = File.expand_path(ARGV.fetch(0, 'build/font-preview/blit-fonts.png'))
Dir.chdir(root) do
  Dir.mktmpdir('blit-font-preview') do |work|
    binary = File.join(work, 'preview')
    headers = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")
    library = ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall', "-I#{headers}",
      'tools/probes/font_preview.c', 'src/blit_font.c', 'src/blit_raster.c', library, '-o', binary)
    ppm = File.join(work, 'specimen.ppm')
    raise 'raster failed' unless system(binary, 'assets/fonts/Blit-Hibr-Terminus16.psf',
      'assets/fonts/BlitRetroNerd-Regular.ttf', 'assets/fonts/BlitModernNerd-Regular.ttf',
      'assets/fonts/NotoColorEmoji.ttf', ppm)
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
