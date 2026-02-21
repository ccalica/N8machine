# N8 Alternate Character Sets — Proposals

Version: 1.0
Date: 2026-02-21

The N8 primary charset (see `n8_charmap.md`) uses all 256 code points. Alternate charsets can be switched in at runtime, sharing the same ASCII core ($20-$7E) but replacing the non-ASCII regions ($00-$1F, $7F, $80-$FF) with different content.

---

## Charset 0: Primary (Default)

The standard charset defined in `n8_charmap.md`. Balanced mix of arrows, blocks, box drawing, international, geometric, math, and symbols.

---

## Charset 1: "Graphics Mode" — Uppercase Only + Full 64 Mosaics

**Concept:** Sacrifice lowercase letters to gain 26 extra graphic slots. Adds the full 64-entry 2x3 block mosaic set for pseudo-pixel graphics (160x50 at 80x25, 160x16 at 80x8).

### Changes from Primary

| Range | Primary | Charset 1 |
|-------|---------|-----------|
| $61-$7A | Lowercase a-z (26) | **Uppercase A-Z duplicate** (same glyphs as $41-$5A) |
| $80-$BF | Blocks (32) + Box (30) + ¡¿ (2) | **Full 64 mosaics** ($80-$BF) |
| $C0-$CF | International (16) | **Box drawing (30)** starts at $C0 |
| $D0-$D5 | Geometric (6) | **Box drawing (cont'd)** |
| $D6-$D9 | Geometric (4) | **¡¿ + blocks leftover (4)** |

### 2x3 Mosaic Encoding ($80-$BF)

Each code point in $80-$BF maps to a 2x3 block pattern. The 6-bit pattern index is the low 6 bits of the code point:

```
Code = $80 + (bit5 << 5) | (bit4 << 4) | (bit3 << 3) | (bit2 << 2) | (bit1 << 1) | bit0

Bit layout in the 2x3 cell:
  +-----+-----+
  | b0  | b1  |  Top row (5 pixels high)
  +-----+-----+
  | b2  | b3  |  Middle row (6 pixels high)
  +-----+-----+
  | b4  | b5  |  Bottom row (5 pixels high)
  +-----+-----+

$80 = all empty (space-like, but distinct from $20)
$BF = all filled (solid block)
```

Pixel geometry: 4px wide columns, 5-6-5 pixel high rows in the 8x16 cell.

### What This Enables

- **Pseudo-pixel resolution:** 160x50 (emulator 80x25) or 160x16 (LCD 80x8)
- **Programmatic bitmap rendering:** Any 160xN bitmap can be converted to mosaic chars by dividing into 2x3 cells
- **Bar charts, waveforms, simple graphics** without custom font work

### What This Loses

- Lowercase letters (text becomes uppercase-only like C64 graphics mode)
- International characters (moved to Charset 2)
- Reduced geometric/symbol repertoire

### Use Cases

- Games using pseudo-pixel graphics
- Data visualization (charts, waveforms, gauges)
- Graphical demos
- Large-format text banners (compose big letters from mosaics)

---

## Charset 2: "International" — Extended Latin + Typography

**Concept:** Replace game/electronics/decorative characters with a comprehensive Western European character set. For text-heavy applications, word processing, or multilingual display.

### Changes from Primary

| Range | Primary | Charset 2 |
|-------|---------|-----------|
| $00-$06 | Smiley, bullet, suits (7) | **Additional accented: Â Ê Î Ô Û â ê** |
| $07-$09 | Bullet, check, X (3) | **î ô û** |
| $0A-$0B | Star, diamond (2) | **ß ð** |
| $D0-$D9 | Geometric + dice (16) | **Typographic: — – ' ' " " … ™ + 8 more accented** |
| $DA-$DF | Dice (6) | **Nordic: Å å Æ æ Ø ø** |
| $F5-$F9 | Electronics (5) | **More accented: Ã ã Õ õ Ú** |
| $FA-$FB | ©® (2) | **ú ù** |

### Total International Coverage

- Primary charset: 18 international chars (Spanish, French, German, Portuguese basics)
- Charset 2: ~55 international chars (adds complete French, Nordic, additional Portuguese/Italian)

### Languages Fully Supported

| Language | Primary | Charset 2 |
|----------|---------|-----------|
| English | Full | Full |
| Spanish | Full | Full |
| German | Full | Full |
| French | Partial (missing â, ê, î, ô, û, ù) | Full |
| Portuguese | Partial (missing ã, õ, ú) | Full |
| Italian | Partial (missing è, ì, ò) | Full |
| Nordic (Swedish/Norwegian/Danish) | None | Full (Å å Æ æ Ø ø) |
| Icelandic | None | Partial (ð, but missing þ) |

### What This Loses

- Card suits and dice
- Geometric outline shapes
- Electronics symbols
- Most decorative characters
- Smiley face

### Use Cases

- Text editors / word processing
- Multilingual message display
- Documentation viewing
- Any application prioritizing text over graphics

---

## Switching Mechanism (Outline)

The charset switch could be implemented as a memory-mapped register:

```
$C1xx  Charset select register (proposed, exact address TBD)
  Write $00 = Primary charset (default)
  Write $01 = Graphics mode (Charset 1)
  Write $02 = International (Charset 2)
```

The emulator would maintain all charset bitmaps in memory and switch the active font pointer on write. The firmware would expose this as a simple API call.

**Important:** ASCII ($20-$7E) is identical across all charsets. Only the 161 non-ASCII positions change. This means:
- Text output works identically regardless of active charset
- Only code that explicitly uses non-ASCII characters ($00-$1F, $7F, $80-$FF) needs to know which charset is active
- Applications can switch charsets mid-screen if the display supports per-row or per-region charset selection (future enhancement)

---

## Summary

| Charset | Name | Strengths | Weaknesses |
|---------|------|-----------|------------|
| 0 | Primary | Balanced, general-purpose | No mosaics, limited international |
| 1 | Graphics | Full 2x3 mosaics, pseudo-pixel mode | No lowercase, no international |
| 2 | International | Complete Western European text | No game graphics, no geometric shapes |
