R = '/tmp/claude-2000/-home-osakka-hw-manager/51f0679e-9abf-43d8-a616-451debc87eb3/scratchpad/frame3/'


def edit(path, pairs):
    s = open(R + path).read()
    for a, b in pairs:
        assert a in s, (path, a[:60])
        s = s.replace(a, b, 1)
    open(R + path, 'w').write(s)


edit('tools/tests/window_style_gate.rb', [
    ("# - The outer-edge frame (ADR 0214, U+1002AE-1002B6): its nine glyphs on a",
     "# - The outer-edge frame (ADR 0214, U+1002AE-1002BA): its thirteen glyphs on a"),
    ("""#   beyond the frame cell starting in the cell""", """#   beyond the frame cell starting in the cell""") if False else
    ("# - SGR 66 (ADR 0211):", """# - Inactive windows (0.3.45), Chalk and Slate, built from the plain title
#   ends: the bar with no stripes anywhere (the end cells, the plain `title`
#   cells between), the outer line on both edges, the body and bottom as in
#   the active windows.
# - SGR 66 (ADR 0211):"""),
    ("nine = (0...9).map", "nine = (0...13).map"),
    ("(i < 5 ? face_pens : bar_pens)", "(i < 5 ? face_pens : bar_pens)"),
    ("""    frames_scene = at.call(2, 10, nine) + win_text.call(2, 12, true, false) + win_text.call(2, 20, false, true)""",
     """    # Inactive windows: plain title ends round plain `title` cells, no stripes.
    inactive_text = lambda do |x0, y0, slate|
      first = slate ? 0x100254 : 0x100231
      ends = [FRAME_FIRST + (slate ? 11 : 9), FRAME_FIRST + (slate ? 12 : 10)]
      title = [ends[0]] + [first + 5] * (WIN_COLS - 2) + [ends[1]]
      out = at.call(x0, y0, bar_pens + title.map { |c| u(c) }.join)
      (1..WIN_BODY).each do |r|
        out << at.call(x0, y0 + r, face_pens + u(FRAME_FIRST) + ' ' * (WIN_COLS - 2) + u(FRAME_FIRST + 1))
      end
      out << at.call(x0, y0 + WIN_BODY + 1, face_pens + u(FRAME_FIRST + 2) + u(FRAME_FIRST + 3) * (WIN_COLS - 2) + u(FRAME_FIRST + 4))
      out << sgr('0') + colours
    end
    frames_scene = at.call(2, 10, nine) + win_text.call(2, 12, true, false) + win_text.call(2, 20, false, true) +
                   inactive_text.call(16, 12, true) + inactive_text.call(16, 20, false)"""),
    ("""    check(lines.size == 79, "the design has #{lines.size} glyphs, not 79")""",
     """    check(lines.size == 83, "the design has #{lines.size} glyphs, not 83")"""),
    ("""    9.times do |i|
      cell_ok.call(2 + i, 10, FRAME_FIRST + i, INK, i < 5 ? FACE : BAR, HI, "frame glyph #{i}")
    end""", """    13.times do |i|
      cell_ok.call(2 + i, 10, FRAME_FIRST + i, INK, i < 5 ? FACE : BAR, HI, "frame glyph #{i}")
    end
    # Inactive windows: every cell the design's; no stripe anywhere in the bar
    # (every pixel off the bevel rows and the outer lines is the bar colour);
    # the body and bottom as the active windows'.
    [[12, true, 0x100254], [20, false, 0x100231]].each do |y0, slate, first|
      w = WIN_COLS
      ends = [FRAME_FIRST + (slate ? 11 : 9), FRAME_FIRST + (slate ? 12 : 10)]
      ([ends[0]] + [first + 5] * (w - 2) + [ends[1]]).each_with_index do |cp, i|
        cell_ok.call(16 + i, y0, cp, INK, BAR, HI, "inactive window title cell #{i}")
      end
      (2...ch - 2).each do |dy|
        (1...w * cw - 1).each do |ax|
          colour = px.call(16 + ax / cw, y0, ax % cw, dy)
          check(colour == BAR, "the inactive #{slate ? 'Slate' : 'Chalk'} bar has a stripe at #{ax},#{dy}: #{colour.inspect}")
          checked += 1
        end
      end
      (1..WIN_BODY + 1).each do |r|
        w.times do |c|
          ch.times do |dy|
            cw.times do |dx|
              bottom = r == WIN_BODY + 1
              line = (c.zero? && dx.zero?) || (c == w - 1 && dx == cw - 1) || (bottom && dy == ch - 1)
              got = px.call(16 + c, y0 + r, dx, dy)
              check(got == (line ? INK : FACE), "inactive window cell #{c},#{r} pixel #{dx},#{dy} is #{got.inspect}")
              checked += 1
            end
          end
        end
      end
    end"""),
    ("glyphs=70+9_frame", "glyphs=70+13_frame inactive_windows=plain_title_ends"),
])
