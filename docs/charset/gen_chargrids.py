#!/usr/bin/env python3
"""Generate character grid SVGs for Directions B and C.
Each character is rendered at 4x zoom (each pixel = 4x4 screen pixels).
Character cell: 8x16 pixels -> 32x64 screen pixels.
Grid layout: 8 chars per row, with gap and labels.
"""

# =========================================================================
# BITMAP DEFINITIONS (8x16, MSB=left pixel)
# =========================================================================

# Shared letters (same across all directions for the grid)
LETTERS = {
    'A': [0x00,0x18,0x3C,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00],
    'B': [0x00,0x7C,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00],
    'C': [0x00,0x3C,0x66,0x60,0x60,0x60,0x60,0x60,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00],
    'M': [0x00,0xC3,0xE7,0xFF,0xDB,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00],
    'W': [0x00,0xC3,0xC3,0xC3,0xC3,0xC3,0xDB,0xFF,0xE7,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00],
    'a': [0x00,0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00,0x00,0x00,0x00,0x00],
    'b': [0x00,0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00],
    'c': [0x00,0x00,0x00,0x00,0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00,0x00,0x00,0x00,0x00],
    'm': [0x00,0x00,0x00,0x00,0xE6,0xFF,0xDB,0xDB,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00],
    'w': [0x00,0x00,0x00,0x00,0xC3,0xC3,0xC3,0xDB,0xFF,0xE7,0xC3,0x00,0x00,0x00,0x00,0x00],
}

NUMBERS = {
    '0': [0x00,0x3C,0x66,0x66,0x6E,0x76,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00],
    '1': [0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00],
    '8': [0x00,0x3C,0x66,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00],
}

# ── Box drawing: Direction B (SHARP corners) ──
BOX_B = {
    'TL':  [0x00]*7 + [0x1F,0x1F] + [0x18]*7,   # ┌
    'H':   [0x00]*7 + [0xFF,0xFF] + [0x00]*7,    # ─
    'TR':  [0x00]*7 + [0xF8,0xF8] + [0x18]*7,   # ┐
    'V':   [0x18]*16,                              # │
    'BL':  [0x18]*7 + [0x1F,0x1F] + [0x00]*7,   # └
    'BR':  [0x18]*7 + [0xF8,0xF8] + [0x00]*7,   # ┘
    'LT':  [0x18]*7 + [0x1F,0x1F] + [0x18]*7,   # ├
    'RT':  [0x18]*7 + [0xF8,0xF8] + [0x18]*7,   # ┤
    # Heavy variants (3px wide)
    'HV':  [0x3C]*16,                             # ┃ heavy vertical
    'HH':  [0x00]*6 + [0xFF]*4 + [0x00]*6,       # ━ heavy horizontal
}

# ── Box drawing: Direction C (CP437 standard) ──
BOX_C = {
    'TL':  [0x00]*7 + [0x1F,0x1F] + [0x18]*7,   # ┌
    'H':   [0x00]*7 + [0xFF,0xFF] + [0x00]*7,    # ─
    'TR':  [0x00]*7 + [0xF8,0xF8] + [0x18]*7,   # ┐
    'V':   [0x18]*16,                              # │
    'BL':  [0x18]*7 + [0x1F,0x1F] + [0x00]*7,   # └
    'BR':  [0x18]*7 + [0xF8,0xF8] + [0x00]*7,   # ┘
    'LT':  [0x18]*7 + [0x1F,0x1F] + [0x18]*7,   # ├
    'RT':  [0x18]*7 + [0xF8,0xF8] + [0x18]*7,   # ┤
    # Double-line variants (CP437)
    'DH':  [0x00]*6 + [0xFF,0x00,0xFF] + [0x00]*7,    # ═
    'DV':  [0x24]*16,                                   # ║
    'DTL': [0x00]*6 + [0x1F,0x10,0x17] + [0x24]*7,    # ╔
    'DTR': [0x00]*6 + [0xF8,0x08,0xE8] + [0x24]*7,    # ╗
}

# ── Block elements (same for both) ──
BLOCKS = {
    'FULL':   [0xFF]*16,
    'UHALF':  [0xFF]*8 + [0x00]*8,
    'LHALF':  [0x00]*8 + [0xFF]*8,
    'LHBLK':  [0xF0]*16,
    'RHBLK':  [0x0F]*16,
    'QTR':    [0xF0]*8 + [0x00]*8,    # Upper-left quarter
    'LIGHT':  [0x22,0x00,0x88,0x00]*4,
    'MED':    [0xAA,0x55]*8,
    'DARK':   [0xDD,0xFF,0x77,0xFF]*4,
}

# ── Arrows ──
ARROWS = {
    'LEFT':  [0x00,0x00,0x00,0x00,0x20,0x60,0xFF,0xFF,0x60,0x20,0x00,0x00,0x00,0x00,0x00,0x00],
    'RIGHT': [0x00,0x00,0x00,0x00,0x04,0x06,0xFF,0xFF,0x06,0x04,0x00,0x00,0x00,0x00,0x00,0x00],
    'UP':    [0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00],
    'DOWN':  [0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00,0x00,0x00],
}

# ── Direction B unique: ATASCII-inspired ──
ATASCII_UNIQUE = {
    # Crisp right-pointing triangle (geometric)
    'TRI_R': [0x00,0x40,0x60,0x70,0x78,0x7C,0x7E,0x7F,0x7E,0x7C,0x78,0x70,0x60,0x40,0x00,0x00],
    # Sharp angular slash pattern
    'DIAG_B': [0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0],
    # Crosshair / targeting reticle
    'XHAIR': [0x00,0x18,0x18,0x18,0xDB,0xFF,0xDB,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00],
}

# ── Direction C unique: CP437-inspired ──
CP437_UNIQUE = {
    # Pi symbol π
    'PI':     [0x00,0x00,0x00,0x00,0x7E,0x24,0x24,0x24,0x24,0x24,0x64,0x00,0x00,0x00,0x00,0x00],
    # House / home symbol ⌂
    'HOUSE':  [0x00,0x18,0x3C,0x7E,0xFF,0xC3,0xDB,0xDB,0xDB,0xFF,0x00,0x00,0x00,0x00,0x00,0x00],
    # Section sign §
    'SECT':   [0x00,0x3C,0x66,0x30,0x3C,0x66,0x66,0x3C,0x0C,0x66,0x3C,0x00,0x00,0x00,0x00,0x00],
}

# ── Shared symbols ──
SYMBOLS = {
    'BULLET': [0x00,0x00,0x00,0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    'DEGREE': [0x00,0x38,0x44,0x44,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    'HEART':  [0x00,0x00,0x00,0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00,0x00,0x00,0x00,0x00,0x00],
    'DIAMOND':[0x00,0x00,0x18,0x3C,0x7E,0xFF,0x7E,0x3C,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
}


def bitmap_to_svg_rects(bitmap, ox, oy, scale=4):
    """Convert 8x16 bitmap to SVG rect elements at given offset."""
    rects = []
    for row, val in enumerate(bitmap):
        for col in range(8):
            if val & (0x80 >> col):
                x = ox + col * scale
                y = oy + row * scale
                rects.append(f'    <rect x="{x}" y="{y}" width="{scale}" height="{scale}"/>')
    return rects


def make_char_grid_svg(chars, title, subtitle, bg, fg, filename, unique_label=""):
    """Generate a character grid SVG.

    chars: list of (label, bitmap) tuples
    """
    scale = 4
    cw = 8 * scale   # 32
    ch = 16 * scale   # 64
    cols_per_row = 8
    gap_x = 12        # horizontal gap between chars
    gap_y = 24        # vertical gap (for label)
    margin = 20
    label_h = 16

    cell_w = cw + gap_x
    cell_h = ch + gap_y + label_h

    nrows = (len(chars) + cols_per_row - 1) // cols_per_row

    svg_w = margin * 2 + cols_per_row * cell_w
    svg_h = margin + 60 + nrows * cell_h + 40  # 60 for title area

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{svg_w}" height="{svg_h}" viewBox="0 0 {svg_w} {svg_h}">',
        f'  <title>{title}</title>',
        f'  <defs><style>',
        f'    .bg {{ fill: {bg}; }}',
        f'    .px {{ fill: {fg}; }}',
        f'    .lbl {{ fill: {fg}; font-family: monospace; font-size: 11px; opacity: 0.7; }}',
        f'    .title {{ fill: {fg}; font-family: monospace; font-size: 18px; }}',
        f'    .sub {{ fill: {fg}; font-family: monospace; font-size: 12px; opacity: 0.6; }}',
        f'  </style></defs>',
        f'  <rect class="bg" width="{svg_w}" height="{svg_h}"/>',
        f'  <text class="title" x="{margin}" y="{margin + 18}">{title}</text>',
        f'  <text class="sub" x="{margin}" y="{margin + 36}">{subtitle}</text>',
    ]

    lines.append(f'  <g class="px">')

    for idx, (label, bitmap) in enumerate(chars):
        row = idx // cols_per_row
        col = idx % cols_per_row
        ox = margin + col * cell_w
        oy = margin + 50 + row * cell_h
        rects = bitmap_to_svg_rects(bitmap, ox, oy, scale)
        lines.extend(rects)
        # Label
        lines.append(f'  <text class="lbl" x="{ox}" y="{oy + ch + 14}">{label}</text>')

    lines.append('  </g>')

    # Footer note
    footer_y = margin + 50 + nrows * cell_h + 10
    lines.append(f'  <text class="sub" x="{margin}" y="{footer_y}">{unique_label}</text>')

    lines.append('</svg>')

    with open(filename, 'w') as f:
        f.write('\n'.join(lines))
    print(f"Generated {filename} ({len(chars)} characters)")


# =========================================================================
# DIRECTION B: "Atari Technical"
# =========================================================================

def gen_dir_b():
    chars = []
    # Letters
    for k in ['A','B','C','M','W','a','b','c','m','w']:
        chars.append((k, LETTERS[k]))
    # Numbers
    for k in ['0','1','8']:
        chars.append((k, NUMBERS[k]))
    # Box drawing (sharp)
    for label, key in [('\u250C TL','TL'),('\u2500 H','H'),('\u2510 TR','TR'),
                        ('\u2502 V','V'),('\u2514 BL','BL'),('\u2518 BR','BR'),
                        ('\u251C LT','LT'),('\u2524 RT','RT'),
                        ('\u2503 hvy','HV'),('\u2501 hvy','HH')]:
        chars.append((label, BOX_B[key]))
    # Blocks
    for label, key in [('\u2588 full','FULL'),('\u2580 top','UHALF'),
                        ('\u2584 btm','LHALF'),('\u258C left','LHBLK'),
                        ('\u2590 right','RHBLK'),('qtr','QTR'),
                        ('\u2591 lite','LIGHT'),('\u2592 med','MED'),('\u2593 dark','DARK')]:
        chars.append((label, BLOCKS[key]))
    # Arrows
    for label, key in [('\u2190','LEFT'),('\u2192','RIGHT'),('\u2191','UP'),('\u2193','DOWN')]:
        chars.append((label, ARROWS[key]))
    # Symbols
    for label, key in [('\u2022 bullet','BULLET'),('\u00B0 degree','DEGREE')]:
        chars.append((label, SYMBOLS[key]))
    # ATASCII unique
    for label, key in [('\u25B6 tri','TRI_R'),('diag B','DIAG_B'),('xhair B','XHAIR')]:
        chars.append((label, ATASCII_UNIQUE[key]))

    make_char_grid_svg(
        chars,
        'Direction B: "Atari Technical" \u2014 Character Grid',
        '8\u00d716 pixel characters \u00b7 4\u00d7 zoom \u00b7 Sharp corners, angular precision, maximum contrast',
        '#1a1a2e',
        '#ffaa00',
        '/home/calica/projects/N8machine/docs/charset/dir_b_chars.svg',
        'Key traits: Sharp 90\u00b0 corners \u00b7 Angular geometric shapes \u00b7 Precise line work \u00b7 ATASCII reticle + triangle + diagonal'
    )


# =========================================================================
# DIRECTION C: "CP437 Utility"
# =========================================================================

def gen_dir_c():
    chars = []
    # Letters
    for k in ['A','B','C','M','W','a','b','c','m','w']:
        chars.append((k, LETTERS[k]))
    # Numbers
    for k in ['0','1','8']:
        chars.append((k, NUMBERS[k]))
    # Box drawing (CP437 style)
    for label, key in [('\u250C TL','TL'),('\u2500 H','H'),('\u2510 TR','TR'),
                        ('\u2502 V','V'),('\u2514 BL','BL'),('\u2518 BR','BR'),
                        ('\u251C LT','LT'),('\u2524 RT','RT'),
                        ('\u2550 dbl','DH'),('\u2551 dbl','DV'),
                        ('\u2554 dbl','DTL'),('\u2557 dbl','DTR')]:
        chars.append((label, BOX_C[key]))
    # Blocks
    for label, key in [('\u2588 full','FULL'),('\u2580 top','UHALF'),
                        ('\u2584 btm','LHALF'),('\u258C left','LHBLK'),
                        ('\u2590 right','RHBLK'),('qtr','QTR'),
                        ('\u2591 lite','LIGHT'),('\u2592 med','MED'),('\u2593 dark','DARK')]:
        chars.append((label, BLOCKS[key]))
    # Arrows
    for label, key in [('\u2190','LEFT'),('\u2192','RIGHT'),('\u2191','UP'),('\u2193','DOWN')]:
        chars.append((label, ARROWS[key]))
    # Symbols
    for label, key in [('\u2022 bullet','BULLET'),('\u00B0 degree','DEGREE'),
                        ('\u2665','HEART'),('\u25C6','DIAMOND')]:
        chars.append((label, SYMBOLS[key]))
    # CP437 unique
    for label, key in [('\u03C0 pi','PI'),('\u2302 house','HOUSE'),('\u00A7 sect','SECT')]:
        chars.append((label, CP437_UNIQUE[key]))

    make_char_grid_svg(
        chars,
        'Direction C: "CP437 Utility" \u2014 Character Grid',
        '8\u00d716 pixel characters \u00b7 4\u00d7 zoom \u00b7 Clean lines, balanced readability, proven CP437 proportions',
        '#1a1a2e',
        '#cccccc',
        '/home/calica/projects/N8machine/docs/charset/dir_c_chars.svg',
        'Key traits: Clean professional lines \u00b7 Double-line box drawing \u00b7 Information density \u00b7 CP437 pi + house + section'
    )


if __name__ == '__main__':
    gen_dir_b()
    gen_dir_c()
    print("\nAll character grids generated.")
