# 2x3 Block Mosaic Analysis for N8

## The Question

Should the N8 primary charset include a full 64-entry 2x3 block mosaic set (like TRS-80/Teletext), a partial set, or skip mosaics entirely?

## Pixel Geometry: 2x3 in an 8x16 Cell

Horizontal: 8px / 2 columns = **4px per column** (exact)
Vertical: 16px / 3 rows = **5.333px** (not exact)

Best division: **5-6-5** (top 5px, middle 6px, bottom 5px). Symmetric top/bottom, asymmetry hidden in middle row.

Sub-block dimensions: 4x5, 4x6, 4x5 pixels. Aspect ratio ~1:1.3 (comparable to CGA 320x200 on 4:3 monitor).

## Effective Resolutions

| Display | Text Size | Mosaic Resolution | Assessment |
|---------|-----------|-------------------|------------|
| Emulator | 80x25 | 160x75 | Useful. Between CGA 160x100 and TRS-80 128x48. |
| Emulator | 80x50 (future) | 160x150 | Genuinely good. |
| LCD | 80x8 | 160x24 | Marginal. Specific uses only (waveforms, bar charts). |

## What 160x75 Can Actually Do

Proven territory — TRS-80 shipped at 128x48 and people drew:
- Bar charts and histograms (75 vertical levels)
- Simple line graphs
- Maze games (playable at this resolution)
- Recognizable 8x10 sprite-like characters
- World maps / geographic outlines
- Progress bars, gauges, waveform displays
- Conway's Game of Life / cellular automata

At 160x75 you have 12,000 pseudo-pixels. This is a legitimate low-resolution graphics mode.

## What 160x24 Can Do (LCD)

Severely limited:
- Horizontal bar charts (up to 24 bars, or vertical with only 24 levels)
- Single-line waveform/scope trace (160 horizontal x 24 vertical — actually decent)
- Chunky status indicators (battery, signal bars)
- Large text banners (3x5 pixel micro-font = ~53x4 tiny characters)

**Not useful for:** Maps, game screens, arbitrary pictures, anything requiring spatial detail.

## Three Options Analyzed

### Option 1: Full 64 Mosaics (64 slots)

**Pros:** Enables programmatic bitmap rendering. Any 160xN image convertible to text. Maximum pseudo-pixel flexibility.

**Cons:** Costs 64 of 123 available graphic slots. Creates "graphics mode" expectations that 160x24 (LCD) can't deliver. The 44 patterns beyond the practical top-20 are rarely used in practice.

**Remaining slots:** 59 for all other graphics.

### Option 2: Partial Set (~20 chars) — RECOMMENDED

The top 20 patterns by real-world usage frequency:

**Tier 1 — Essential (12):**
- Half blocks: upper, lower, left, right (4)
- Third blocks: upper 1/3, lower 1/3, upper 2/3, lower 2/3 (4)
- Quadrants: UL, UR, LL, LR (4)

**Tier 2 — High value (8):**
- Full block (1)
- Shading: light 25%, medium 50%, dark 75% (3)
- Diagonal triangles: ◢ ◣ ◤ ◥ (4)

These 20 patterns handle: bar charts (3x vertical resolution), area fills (4 density levels), quarter-block plotting (2x2 pseudo-pixels = 160x50 effective), and crude diagonals. This covers ~90% of practical mosaic usage.

**Remaining slots:** 103 for rich graphic characters.

### Option 3: No Mosaics (0 slots)

**Remaining slots:** 123 for graphics. Very rich symbol set possible but zero pseudo-pixel capability. For a text-only machine, this eliminates the only "graphics mode" escape hatch.

## Recommendation

**Partial set of 20 in the primary charset. Full 64 in an alternate charset.**

Rationale:
1. **The LCD kills the full-64 argument.** At 80x8, full mosaics give 160x24 — not worth 64 slots.
2. **80/20 rule applies hard.** 20 patterns cover 90%+ of real usage. The long tail has sharply diminishing returns.
3. **Monochrome amplifies symbol value.** Without color, distinct graphic characters are your only tool for visual distinction. 103 rich symbols > 59 symbols + 44 rarely-used mosaics.
4. **Full mosaics available via charset switch.** Charset 1 ("Graphics Mode") trades lowercase letters for all 64 mosaics. Programs that need pseudo-pixel mode can opt in.

## The 20 Characters in the Primary Charset

Placed at $80-$9F (see `n8_charmap.md` for full details):

```
$80  █  Full block
$81  ▀  Upper half       $82  ▄  Lower half
$83  ▌  Left half        $84  ▐  Right half
$85  ▘  Quad UL          $86  ▝  Quad UR
$87  ▖  Quad LL          $88  ▗  Quad LR
$89  ▚  Quad UL+LR       $8A  ▞  Quad UR+LL
$8B  ▛  3/4 (no LR)      $8C  ▜  3/4 (no LL)
$8D  ▙  3/4 (no UR)      $8E  ▟  3/4 (no UL)
$8F  ░  Light shade
$90  ▒  Medium shade      $91  ▓  Dark shade
$92-$97  Third blocks (N8-specific, upper/lower/left/right 1/3 and 2/3)
$98-$9B  Diagonal triangles (◢◣◤◥)
$9C-$9E  Diagonal lines (╱╲╳)
$9F  ▬  Horizontal bar
```

Total: 32 block element characters (expanded from original 20 budget to include all quadrant combinations and diagonal elements).
