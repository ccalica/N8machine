# N8 Charset Aesthetic Directions

## Three Directions Explored

### Direction A: "Commodore Heritage"

**Visual signature:** Rounded corners, organic shapes, playful weight.

**Key traits:**
- Box drawing uses 1px rounded corners on all four corner pieces
- Card suits are organic/rounded (heart has curves, spade has smooth leaf shape)
- Diagonal elements use smooth curves rather than staircase pixels
- Overall feel: friendly, approachable, game-oriented
- Monochrome color: green phosphor (#33ff33 on dark background)

**Inspiration sources:**
- PETSCII's rounded graphic characters
- C64's characteristic "soft" aesthetic
- The friendly, accessible design philosophy of Commodore machines

**Best for:** Games, demos, creative applications, retro aesthetic projects.

**SVG mockups:** `dir_a_chars.svg` (character grid), `dir_a_screen.svg` (file manager TUI)

---

### Direction B: "Atari Technical"

**Visual signature:** Sharp 90-degree corners, angular precision, maximum contrast.

**Key traits:**
- Box drawing uses hard right-angle corners throughout
- All shapes are geometric and angular (triangular arrows are sharp, no curves)
- Heavy line variants are notably thicker (3px vs 1px)
- Crosshair/targeting reticle as signature character
- Overall feel: technical, precise, mechanical
- Monochrome color: amber phosphor (#ffaa00 on dark background)

**Inspiration sources:**
- ATASCII's sharp geometric vocabulary
- Atari 800/monochrome monitor aesthetic
- Technical/engineering instrument displays

**Best for:** System monitors, debuggers, technical tools, engineering applications.

**SVG mockups:** `dir_b_chars.svg` (character grid), `dir_b_screen.svg` (system monitor TUI)

---

### Direction C: "CP437 Utility"

**Visual signature:** Clean lines, balanced proportions, information-dense.

**Key traits:**
- Box drawing includes both single and double-line variants (CP437's signature)
- Character proportions optimized for readability at small sizes
- Shade characters (░▒▓) designed for smooth gradient appearance
- Pi, house, section sign as signature symbols
- Overall feel: professional, utilitarian, information-focused
- Monochrome color: white/gray phosphor (#cccccc on dark background)

**Inspiration sources:**
- IBM CP437 on VGA (the gold standard for TUI charsets)
- DOS-era Norton Commander, Turbo Pascal IDE
- Professional terminal emulator aesthetics

**Best for:** Dashboards, information displays, system administration tools, professional TUI applications.

**SVG mockups:** `dir_c_chars.svg` (character grid), `dir_c_screen.svg` (dashboard TUI)

---

## Comparison

| Trait | A: Commodore | B: Atari | C: CP437 |
|-------|-------------|----------|----------|
| Corner style | Rounded | Sharp 90° | Sharp 90° (single+double) |
| Line weight | Medium | Heavy/thin contrast | Uniform thin |
| Shape language | Organic curves | Angular geometry | Balanced/neutral |
| Personality | Playful | Technical | Professional |
| Best display | Green phosphor | Amber phosphor | White/gray |
| LCD suitability | Good | Excellent (high contrast) | Good |
| Information density | Medium | Medium-high | High |
| Game suitability | Excellent | Good | Fair |
| TUI suitability | Good | Good | Excellent |

## Recommendation

The N8 primary charset design in `n8_charmap.md` takes a **hybrid approach**:

- **Box drawing:** Direction C's approach — single + heavy lines (not double, which costs too many slots). Plus Direction A's rounded corners as optional alternatives. This gives 3 visual tiers (rounded for friendly UI, single for standard, heavy for emphasis).
- **Geometric shapes:** Direction B's sharp angular style — more legible at 8x16 than curves.
- **Symbols:** Direction A's card suits and organic shapes — these are the characters that benefit most from curves.
- **Overall tone:** Closer to C (utility-focused) with A's personality in the decorative characters.

The aesthetic choice ultimately comes down to the font bitmap design (a future task). The code point map in `n8_charmap.md` is aesthetic-neutral — it defines *what* each character is, not *how* it looks at the pixel level. The same map could be rendered in any of the three directions.

## LCD Mockup

`lcd_mockup.svg` demonstrates a compact 80x8 system status display optimized for the 640x128 monochrome LCD target. It shows that 8 rows can contain:
- Title bar with machine name and register state
- RAM/ROM usage bars using shade characters
- IRQ status, cycle counter, current disassembly line
- Command shortcut bar

This validates that the charset design supports dense information display on the height-constrained LCD.
