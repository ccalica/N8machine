# N8 Character Set

## Overview

Custom 256-entry character set for the N8 6502 homebrew computer. 255 displayable glyphs plus $00 (null, renders blank). No control codes, no inverse bit duplication. Based on ASCII with heavy inspiration from Commodore PETSCII, Atari ATASCII, and IBM CP437.

**Display targets:**
- 80x25 text mode (emulator GUI, ~2KB framebuffer)
- 80x8 monochrome LCD (640x128 pixels, real hardware target)

**Character cell:** 8x16 pixels

**Status:** ✅ Font bitmaps complete, ready for emulator integration

---

## Font Files (Ready to Use)

| File | Description |
|------|-------------|
| `n8_font.h` | C header — `uint8_t n8_font[256][16]` for emulator |
| `n8_font.py` | Python dict — for tooling and scripts |
| `n8_font_white.svg` | 16x16 grid preview (white phosphor) |
| `n8_font_green.svg` | 16x16 grid preview (green phosphor) |
| `n8_font_amber.svg` | 16x16 grid preview (amber phosphor) |

**Generator:** `python3 gen_n8font.py` — regenerate all outputs

**License:** CC0 Public Domain (ASCII based on [PCFace Modern DOS 8x16](https://github.com/susam/pcface))

### Hybrid Aesthetic

The font uses a **hybrid approach** combining three aesthetic directions:

- **Box drawing:** Sharp corners (C) + rounded corners (A) + heavy lines (B)
- **Geometric shapes:** Sharp angular precision (Direction B / Atari)
- **Card suits:** Organic curves (Direction A / Commodore)
- **Overall tone:** Utility-focused (C) with personality in decorative chars (A)

---

## Design Documents

| File | Description |
|------|-------------|
| `n8_charmap.md` | Complete 256-entry code point map with Unicode references |
| `alternate_charsets.md` | Proposals for switchable alternate charsets (Graphics, International) |
| `mosaic_analysis.md` | 2x3 block mosaic tradeoff analysis |
| `aesthetic_notes.md` | Aesthetic direction research and comparison |
| `trs80_mosaics.md` | TRS-80 semigraphics system explainer with diagrams |

## SVG Mockups

### Character Grids (4x zoom, ~40 representative chars each)

| File | Direction | Color |
|------|-----------|-------|
| `dir_a_chars.svg` | Commodore Heritage — rounded, organic | Green phosphor |
| `dir_b_chars.svg` | Atari Technical — sharp, angular | Amber phosphor |
| `dir_c_chars.svg` | CP437 Utility — clean, professional | White phosphor |

### Screen Mockups (80x25, 2x zoom)

| File | Direction | Application |
|------|-----------|-------------|
| `dir_a_screen.svg` | Commodore Heritage | File manager (dual pane) |
| `dir_b_screen.svg` | Atari Technical | System monitor (registers, memory, disasm) |
| `dir_c_screen.svg` | CP437 Utility | Dashboard (CPU status, memory map, usage bars) |

### LCD Mockup (80x8, 2x zoom)

| File | Description |
|------|-------------|
| `lcd_mockup.svg` | Compact system status display for 640x128 monochrome LCD |

### Hybrid Aesthetic Mockups

| File | Description |
|------|-------------|
| `hybrid_chars_white.svg` | Character grid — hybrid style, white phosphor |
| `hybrid_chars_green.svg` | Character grid — hybrid style, green phosphor |
| `hybrid_chars_amber.svg` | Character grid — hybrid style, amber phosphor |
| `hybrid_debugger_white.svg` | N8 debugger UI mockup, white phosphor |
| `hybrid_debugger_green.svg` | N8 debugger UI mockup, green phosphor |
| `hybrid_debugger_amber.svg` | N8 debugger UI mockup, amber phosphor |
| `hybrid_game_white.svg` | Game menu mockup, white phosphor |
| `hybrid_game_green.svg` | Game menu mockup, green phosphor |
| `hybrid_game_amber.svg` | Game menu mockup, amber phosphor |

**Note:** Screen mockups have minor rendering artifacts (box-drawing corner glyphs show as "B" in places, some text truncation). Character grids are clean and are the better reference for aesthetic comparison.

## Key Design Decisions

### ASCII Compatibility
Standard printable ASCII at $20-$7E. No remapping. cc65 output works directly.

### Full Upper + Lowercase
Both cases in the primary charset (like ATASCII/CP437, unlike PETSCII mode 1). An alternate "Graphics Mode" charset swaps lowercase for 2x3 block mosaics.

### Block Mosaics: Partial Set (34 chars)
After analysis, the full 64-entry 2x3 mosaic set was deferred to an alternate charset. The primary charset includes 34 block elements: half blocks, quadrants (all 10 non-trivial combinations), third blocks (all 8 — horizontal and vertical, 1/3 and 2/3), shading levels, diagonal triangles, and diagonal lines. These cover ~90% of practical block graphics use cases.

The full 64-mosaic set lives in Charset 1 ("Graphics Mode"), which trades lowercase letters for pseudo-pixel capability (160x50 at 80x25, 160x16 at 80x8).

**Rationale:** On the 80x8 LCD, full mosaics yield only 160x24 pseudo-pixels — too limited for general graphics. The 44 slots saved by going partial are more valuable as distinct symbols, especially in monochrome where visual variety cannot come from color.

### Box Drawing: Single + Heavy + Rounded + Dashed (30 chars)
The most generous box-drawing allocation of any 8-bit charset. Single-line (11), heavy-line (11), rounded corners (4), and dashed variants (4). No double-line set — heavy lines serve the same visual hierarchy purpose with fewer characters.

### International: Small Set (19 chars)
Covers Spanish, French, German (including ß), Portuguese basics. An alternate "International" charset expands to ~55 characters covering full Western European text.

### Monochrome Priority
Designed monochrome-first for the LCD target. Shading patterns, geometric shapes, and distinct symbols provide visual hierarchy without color. Color attributes can be added later as a separate enhancement.

### 8x16 Cell Size
Matches VGA-era 8x16 bitmap fonts (CP437 on VGA). Provides good text readability and enough vertical resolution for recognizable graphic symbols. Block elements use native 8x16 geometry (not scaled from 8x8). Third-block characters exploit the 16-row height with 5-6-5 row subdivision.

## Character Map Summary

```
     _0   _1   _2   _3   _4   _5   _6   _7   _8   _9   _A   _B   _C   _D   _E   _F
0_  NULL  ☺    ●    ♥    ♦    ♣    ♠    …    ✓    ✗    ★    ß    ←    →    ↑    ↓
1_   ↵    ⇐    ⇒    ⇑    ⇓    ▶    ◀    ▲    ▼    ↔    ↕    ⌂    ♪    ♫    §    ¶
2_  SPC   !    "    #    $    %    &    '    (    )    *    +    ,    -    .    /
3_   0    1    2    3    4    5    6    7    8    9    :    ;    <    =    >    ?
4_   @    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O
5_   P    Q    R    S    T    U    V    W    X    Y    Z    [    \    ]    ^    _
6_   `    a    b    c    d    e    f    g    h    i    j    k    l    m    n    o
7_   p    q    r    s    t    u    v    w    x    y    z    {    |    }    ~    ⌐
8_   █    ▀    ▄    ▌    ▐    ▘    ▝    ▖    ▗    ▚    ▞    ▛    ▜    ▙    ▟    ░
9_   ▒    ▓    ⅓↑   ⅓↓   ⅓←   ⅓→   ⅔↑   ⅔↓   ◢    ◣    ◤    ◥    ╱    ╲    ╳    ▬
A_   ─    │    ┌    ┐    └    ┘    ├    ┤    ┬    ┴    ┼    ━    ┃    ┏    ┓    ┗
B_   ┛    ┣    ┫    ┳    ┻    ╋    ╭    ╮    ╰    ╯    ╌    ╎    ╍    ╏    ¡    ¿
C_   À    Á    Ä    Ç    É    Ñ    Ö    Ü    à    á    ä    ç    é    ñ    ö    ü
D_   ○    ◎    □    ■    △    ▷    ▽    ◁    ◇    ☆    ⚀    ⚁    ⚂    ⚃    ⚄    ⚅
E_   ±    ×    ÷    ≠    ≤    ≥    ≈    °    ∞    √    π    Σ    σ    μ    Ω    δ
F_   ¢    £    ¥    €    ¤    ⏻    ⏚    ⚡   ⌖    ⌘    ©    ®    «    »    ⅔←  ⅔→
```

## Category Breakdown (256 total)

| Category | Count | Range |
|----------|-------|-------|
| NULL | 1 | $00 |
| ASCII | 95 | $20-$7E |
| Arrows | 15 | $0C-$1A |
| Block elements | 34 | $80-$9F, $FE-$FF |
| Box drawing | 30 | $A0-$BD |
| International | 19 | $0B, $BE-$CF |
| Geometric | 12 | $02, $0A, $D0-$D9 |
| Card suits + dice | 10 | $03-$06, $DA-$DF |
| Math/Greek | 16 | $E0-$EF |
| Symbols | 12 | $01, $07-$09, $1B-$1F, $7F, $FA-$FB |
| Currency | 5 | $F0-$F4 |
| Electronics | 5 | $F5-$F9 |
| Decorative | 2 | $FC-$FD |

---

## Next Steps

### Immediate: Emulator Integration

1. **Include `n8_font.h` in emulator** — Add to `src/` and include in build
2. **Replace ImGui font for text display** — Currently uses ProggyClean; swap to N8 font for `$C000` framebuffer rendering
3. **Update `emulator.cpp`** — Render characters using `n8_font[char_code]` bitmap data
4. **Test with firmware** — Verify all 256 characters display correctly

### Future: Charset Switching

5. **Implement charset register** — Memory-mapped at `$C1xx` to select active charset
6. **Create Charset 1 (Graphics Mode)** — Full 64-entry 2x3 mosaics, trades lowercase
7. **Create Charset 2 (International)** — Expanded Latin for Western European languages

### Future: Hardware

8. **LCD driver** — Render N8 font on real 640x128 monochrome display
9. **Font ROM** — Convert to binary blob for hardware font ROM

---

## TODOs

- [ ] Integrate `n8_font.h` into emulator build
- [ ] Render N8 font in text display window (replace ProggyClean)
- [ ] Add charset preview/debug window to emulator GUI
- [ ] Review glyph quality at 1x scale (no zoom) — may need tweaks
- [ ] Create Charset 1 (Graphics Mode) with full 2x3 mosaics
- [ ] Create Charset 2 (International) with expanded Latin
- [ ] Implement runtime charset switching via `$C1xx` register
- [ ] Document font format for firmware developers

---

## Generator Scripts

| Script | Purpose |
|--------|---------|
| `gen_n8font.py` | Generate complete 256-char font (C header, Python dict, SVG previews) |
| `gen_hybrid.py` | Generate hybrid aesthetic mockups (char grids, screen mockups) |
| `gen_chargrids.py` | Generate Direction B/C character grids |
| `gen_screens.py` | Generate Direction A/B/C screen mockups |
