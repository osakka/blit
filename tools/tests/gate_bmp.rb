# frozen_string_literal: true

# A screenshot from /v1/diagnostics/screenshots/<display>, whatever backend
# wrote it: SDL writes 24-bit rows, native KMS 32-bit; both are BI_RGB,
# usually bottom-up. Gates read pixels through this, never by hardcoding a
# header size or a stride.
class GateBmp
  attr_reader :width, :height, :bytes

  def initialize(bytes)
    raise 'screenshot is not a BMP' unless bytes.byteslice(0, 2) == 'BM'
    @bytes = bytes
    @offset = bytes.byteslice(10, 4).unpack1('V')
    @width = bytes.byteslice(18, 4).unpack1('l<')
    raw_height = bytes.byteslice(22, 4).unpack1('l<')
    @top_down = raw_height.negative?
    @height = raw_height.abs
    bits = bytes.byteslice(28, 2).unpack1('v')
    raise "unsupported BMP depth #{bits}" unless [24, 32].include?(bits)
    @step = bits / 8
    @stride = ((@width * @step) + 3) & ~3
  end

  def inside?(x, y)
    x >= 0 && y >= 0 && x < @width && y < @height
  end

  # [r, g, b] at (x, y) from the top-left.
  def pixel(x, y)
    position = position(x, y)
    [@bytes.getbyte(position + 2), @bytes.getbyte(position + 1), @bytes.getbyte(position)]
  end

  def each_pixel
    @height.times do |y|
      row = position(0, y)
      @width.times do |x|
        p = row + x * @step
        yield x, y, [@bytes.getbyte(p + 2), @bytes.getbyte(p + 1), @bytes.getbyte(p)]
      end
    end
  end

  def count(rgb)
    total = 0
    each_pixel { |_, _, value| total += 1 if value == rgb }
    total
  end

  private

  def position(x, y)
    row = @top_down ? y : @height - 1 - y
    @offset + row * @stride + x * @step
  end
end

# [r, g, b] at (x, y) of a screenshot's bytes, parsing its header once.
def gate_pixel(bmp, x, y)
  @gate_bmp = GateBmp.new(bmp) unless @gate_bmp&.bytes.equal?(bmp)
  @gate_bmp.pixel(x, y)
end

# The same pixel packed as 0xRRGGBB.
def gate_pixel_rgb24(bmp, x, y)
  r, g, b = gate_pixel(bmp, x, y)
  (r << 16) | (g << 8) | b
end
