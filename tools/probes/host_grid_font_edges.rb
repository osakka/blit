#!/usr/bin/env ruby
# Bounded physical grid/glyph check. Creates only a temporary free-pane PTY.
require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
def req(method, path, body = nil)
  q = {'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,
       'PATCH'=>Net::HTTP::Patch,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE + path)
  q['Authorization'] = "Bearer #{TOKEN}"
  q['Content-Type'] = 'application/json' if body
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 30) { |h| h.request(q) }
  raise "#{method} #{path}: #{r.code}" unless r.code.start_with?('2')
  r
end
def get(path) = JSON.parse(req('GET', path).body)
def change(method, path, body)
  JSON.parse(req(method, path, body).body)
end

topology = get('/v1/topology')
pane = topology.fetch('panes').find { |p| p['surface_id'].nil? }
raise 'requires one free pane; never overwrites the desktop' unless pane
grid = pane.fetch('cell_grid'); region = pane.fetch('pixel_region')
inset = get('/v1/diagnostics').dig('render', 'surface_inset_px')
ox = grid['origin_x'] + inset; oy = grid['origin_y'] + inset
raise 'grid overflows its physical region' unless
  ox >= region['x'] && oy >= region['y'] &&
  ox + grid['pixel_width'] <= region['x'] + region['width'] &&
  oy + grid['pixel_height'] <= region['y'] + region['height']
raise 'requires 8x16 font at scale1' unless grid['cell_width'] == 8 && grid['cell_height'] == 16
active = get('/v1/fonts').fetch('active')
font_name = File.basename(active.fetch('path'))
raise 'requires a checked-in Blit Hibr font' unless
  %w[Blit-Hibr-Terminus16.psf Blit-Hibr-Spleen16.psf].include?(font_name)
font = File.binread(File.expand_path("../../assets/fonts/#{font_name}", __dir__))
magic, _version, header, _flags, count, size, height, width = font[0,32].unpack('L<8')
raise 'font identity/geometry mismatch' unless magic == 0x864ab572 && count == active['glyphs'] && width == 8 && height == 16
mapping = {}
font[(header + count * size)..].split("\xff".b).first(count).each_with_index do |entry, index|
  entry.split("\xfe".b).first.to_s.dup.force_encoding('UTF-8').each_codepoint { |cp| mapping[cp] ||= index }
end
original = get('/v1/terminal-sessions').fetch('sessions').find { |s| s['focused'] }
session = nil
begin
  session = change('POST', '/v1/terminal-sessions',
    {command:'/bin/sh',arguments:['-c','sleep 120'],pane_id:pane['id']})
  change('PATCH', "/v1/terminal-sessions/#{original['id']}", {focused:true}) if original
  cells = [
    {x:0,y:1,text:' ',style:{background:'#123456',foreground:'#abcdef'}},
    {x:grid['columns']-1,y:1,text:' ',style:{background:'#123456',foreground:'#abcdef'}},
    {x:grid['columns']-1,y:2,text:'◆',style:{background:'#123456',foreground:'#abcdef',bold:true}}
  ]
  '◆◷▦⌨◎☺ⓘ◐⎘▤▣◈⇅◉◌⌘⚙▭'.each_char.with_index do |ch, i|
    cells << {x:i+1,y:4,text:ch,style:{background:'#123456',foreground:'#abcdef'}}
  end
  (33..126).each_with_index do |cp,i|
    columns = grid['columns'] - 2
    x = 1 + i % columns; y = 6 + i / columns
    raise 'requires enough rows for the ASCII specimen' unless y < grid['rows']
    cells << {x:x,y:y,text:cp.chr,style:{background:'#123456',foreground:'#abcdef'}}
  end
  surface = session.fetch('surface_id')
  rev = get("/v1/surfaces/#{surface}/summary").fetch('revision')
  receipt = nil
  cells.each_slice(16) do |chunk|
    receipt = change('PUT', "/v1/surfaces/#{surface}/cells", {base_revision:rev,cells:chunk})
    rev = receipt.fetch('revision')
  end
  shot = nil; captured = {}
  30.times do
    shot = req('GET', "/v1/diagnostics/screenshots/#{pane['display_id']}")
    captured = (shot['X-Blit-Capture-Revisions'] || '').split(',').to_h { |v| v.split('=') }
    break if captured[surface].to_i == receipt['revision']
    sleep 0.02
  end
  raise 'capture revision mismatch' unless captured[surface].to_i == receipt['revision']
  bmp = shot.body; offset = bmp[10,4].unpack1('V'); w = bmp[18,4].unpack1('l<'); h = bmp[22,4].unpack1('l<')
  bpp = bmp[28,2].unpack1('S<') / 8; raise 'invalid BMP depth' unless [3,4].include?(bpp)
  stride = (w * bpp + 3) & ~3
  checked = 0
  cells.each do |cell|
    glyph = mapping.fetch(cell[:text].ord)
    16.times do |row|
      bits = cell[:text] == ' ' ? 0 : font.getbyte(header + glyph * size + row)
      bits |= bits >> 1 if cell[:style][:bold]
      8.times do |col|
        want = (bits & (128 >> col)).zero? ? [0x12,0x34,0x56] : [0xab,0xcd,0xef]
        x = ox + cell[:x]*8 + col; y = oy + cell[:y]*16 + row
        raise 'cell was clipped outside framebuffer' unless x.between?(0,w-1) && y.between?(0,h-1)
        p = offset + (h-1-y)*stride + x*bpp
        got = [bmp.getbyte(p+2),bmp.getbyte(p+1),bmp.getbyte(p)]
        raise "GRID_FONT_FAIL cell=#{cell[:x]},#{cell[:y]} pixel=#{col},#{row} got=#{got} want=#{want}" unless got == want
        checked += 1
      end
    end
  end
  if (out = ENV['BLIT_EVIDENCE_DIR'])
    Dir.mkdir(out) unless Dir.exist?(out)
    File.binwrite(File.join(out,'grid-font.bmp'),bmp)
    File.write(File.join(out,'verdict.json'),JSON.pretty_generate({pixels:checked,cells:cells.size,inset:inset,origin:[ox,oy],font_glyphs:count,revision:receipt['revision']}))
  end
  puts "LIVE_GRID_FONT_PASS cells=#{cells.size} pixels=#{checked} edge_cells=complete bold_diamond=complete font_glyphs=#{count} inset=#{inset}"
ensure
  req('DELETE', "/v1/terminal-sessions/#{session['id']}") if session
  change('PATCH', "/v1/terminal-sessions/#{original['id']}", {focused:true}) if original
end
