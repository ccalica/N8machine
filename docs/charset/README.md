# N8 Character Set — Design Research

## Overview

Custom 256-entry character set for the N8 6502 homebrew computer. All entries are displayable glyphs — no control codes, no inverse bit duplication. Based on ASCII with heavy inspiration from Commodore PETSCII, Atari ATASCII, and IBM CP437.

**Display targets:**
- 80x25 text mode (emulator GUI, ~2KB framebuffer)
- 80x8 monochrome LCD (640x128 pixels, real hardware target)

**Character cell:** 8x16 pixels

## Documents

| File | Description |
|------|-------------|
| `n8_charmap.md` | Complete 256-entry code point map with Unicode references |
| `alternate_charsets.md` | Proposals for switchable alternate charsets (Graphics, International) |
| `mosaic_analysis.md` | 2x3 block mosaic tradeoff analysis |
| `aesthetic_notes.md` | Aesthetic direction research and comparison |

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

**Note:** Screen mockups have minor rendering artifacts (box-drawing corner glyphs show as "B" in places, some text truncation). Character grids are clean and are the better reference for aesthetic comparison.

## Key Design Decisions

### ASCII Compatibility
Standard printable ASCII at $20-$7E. No remapping. cc65 output works directly.

### Full Upper + Lowercase
Both cases in the primary charset (like ATASCII/CP437, unlike PETSCII mode 1). An alternate "Graphics Mode" charset swaps lowercase for 2x3 block mosaics.

### Block Mosaics: Partial Set (20 chars)
After analysis, the full 64-entry 2x3 mosaic set was deferred to an alternate charset. The primary charset includes 20 block elements: half blocks, quadrants (all 10 non-trivial combinations), third blocks, shading levels, diagonal triangles, and diagonal lines. These cover ~90% of practical block graphics use cases.

The full 64-mosaic set lives in Charset 1 ("Graphics Mode"), which trades lowercase letters for pseudo-pixel capability (160x50 at 80x25, 160x16 at 80x8).

**Rationale:** On the 80x8 LCD, full mosaics yield only 160x24 pseudo-pixels — too limited for general graphics. The 44 slots saved by going partial are more valuable as distinct symbols, especially in monochrome where visual variety cannot come from color.

### Box Drawing: Single + Heavy + Rounded + Dashed (30 chars)
The most generous box-drawing allocation of any 8-bit charset. Single-line (11), heavy-line (11), rounded corners (4), and dashed variants (4). No double-line set — heavy lines serve the same visual hierarchy purpose with fewer characters.

### International: Small Set (18 chars)
Covers Spanish, French, German, Portuguese basics. An alternate "International" charset expands to ~55 characters covering full Western European text.

### Monochrome Priority
Designed monochrome-first for the LCD target. Shading patterns, geometric shapes, and distinct symbols provide visual hierarchy without color. Color attributes can be added later as a separate enhancement.

### 8x16 Cell Size
Matches VGA-era 8x16 bitmap fonts (CP437 on VGA). Provides good text readability and enough vertical resolution for recognizable graphic symbols. Block elements use native 8x16 geometry (not scaled from 8x8). Third-block characters exploit the 16-row height with 5-6-5 row subdivision.

## Character Map Summary

```
     _0   _1   _2   _3   _4   _5   _6   _7   _8   _9   _A   _B   _C   _D   _E   _F
0_  null  ☺    ●    ♥    ♦    ♣    ♠    •    ✓    ✗    ★    ◆    ←    →    ↑    ↓
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
F_   ¢    £    ¥    €    ¤    ⏻    ⏚    ⎍    ⌖    ⌘    ©    ®    «    »    ‹    ›
```

## Category Breakdown (256 total)

| Category | Count | Range |
|----------|-------|-------|
| ASCII | 95 | $20-$7E |
| Arrows | 19 | $0C-$1A |
| Block elements | 32 | $80-$9F |
| Box drawing | 30 | $A0-$BD |
| International | 18 | $BE-$CF |
| Geometric | 14 | $02,$07,$0A-$0B,$D0-$D9 |
| Card suits + dice | 10 | $03-$06,$DA-$DF |
| Math/Greek | 16 | $E0-$EF |
| Symbols | 12 | $00-$01,$08-$09,$1B-$1F,$7F,$FA-$FB |
| Currency | 5 | $F0-$F4 |
| Electronics | 5 | $F5-$F9 |
| Decorative | 4 | $FC-$FF |
