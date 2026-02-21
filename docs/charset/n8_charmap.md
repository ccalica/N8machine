# N8 Character Map — Complete 256-Entry Code Point Assignment

Version: 1.0
Date: 2026-02-21

## Design Principles

- All 256 entries are displayable glyphs (no control codes)
- All 256 entries are visually distinct (no duplicates)
- No inverse bit — inverse video handled separately by the display hardware
- Standard printable ASCII at $20-$7E (95 chars, fixed)
- 8x16 character cells, two targets: 80x25 (emulator) and 80x8 (monochrome LCD)

## Code Point Layout Strategy

| Range     | Count | Purpose                                       |
|-----------|-------|-----------------------------------------------|
| $00-$1F   | 32    | High-priority graphic chars (arrows, bullets, common symbols, check/X) |
| $20-$7E   | 95    | Standard ASCII (fixed)                        |
| $7F       | 1     | Reversed not sign (replaces DEL)              |
| $80-$9F   | 32    | Block elements, mosaics, shading              |
| $A0-$BD   | 30    | Box drawing (single, heavy, rounded, dashed)  |
| $BE-$CF   | 18    | International / accented characters           |
| $D0-$DF   | 16    | Geometric shapes, card suits, dice            |
| $E0-$EF   | 16    | Mathematical / Greek symbols                  |
| $F0-$FF   | 16    | Currency, electronics, decorative, misc       |

---

## Row $0x: Arrows, Pointers, Common Symbols

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $00  | ---  | (none)  | Null glyph / empty box  | SYMBOL   |
| $01  | ☺    | U+263A  | Smiley face             | SYMBOL   |
| $02  | ●    | U+25CF  | Black circle (bullet)   | GEOM     |
| $03  | ♥    | U+2665  | Black heart suit        | SUIT     |
| $04  | ♦    | U+2666  | Black diamond suit      | SUIT     |
| $05  | ♣    | U+2663  | Black club suit         | SUIT     |
| $06  | ♠    | U+2660  | Black spade suit        | SUIT     |
| $07  | •    | U+2022  | Bullet                  | GEOM     |
| $08  | ✓    | U+2713  | Check mark              | SYMBOL   |
| $09  | ✗    | U+2717  | Ballot X                | SYMBOL   |
| $0A  | ★    | U+2605  | Black star              | GEOM     |
| $0B  | ◆    | U+25C6  | Black diamond           | GEOM     |
| $0C  | ←    | U+2190  | Leftward arrow          | ARROW    |
| $0D  | →    | U+2192  | Rightward arrow         | ARROW    |
| $0E  | ↑    | U+2191  | Upward arrow            | ARROW    |
| $0F  | ↓    | U+2193  | Downward arrow          | ARROW    |

## Row $1x: More Arrows, Pointers, Misc Symbols

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $10  | ↵    | U+21B5  | Return / enter arrow         | ARROW    |
| $11  | ⇐    | U+21D0  | Leftward double arrow        | ARROW    |
| $12  | ⇒    | U+21D2  | Rightward double arrow       | ARROW    |
| $13  | ⇑    | U+21D1  | Upward double arrow          | ARROW    |
| $14  | ⇓    | U+21D3  | Downward double arrow        | ARROW    |
| $15  | ▶    | U+25B6  | Right-pointing triangle      | ARROW    |
| $16  | ◀    | U+25C0  | Left-pointing triangle       | ARROW    |
| $17  | ▲    | U+25B2  | Up-pointing triangle         | ARROW    |
| $18  | ▼    | U+25BC  | Down-pointing triangle       | ARROW    |
| $19  | ↔    | U+2194  | Left-right arrow             | ARROW    |
| $1A  | ↕    | U+2195  | Up-down arrow                | ARROW    |
| $1B  | ⌂    | U+2302  | House                        | SYMBOL   |
| $1C  | ♪    | U+266A  | Eighth note                  | SYMBOL   |
| $1D  | ♫    | U+266B  | Beamed eighth notes          | SYMBOL   |
| $1E  | §    | U+00A7  | Section sign                 | SYMBOL   |
| $1F  | ¶    | U+00B6  | Pilcrow / paragraph sign     | SYMBOL   |

## Row $2x: ASCII $20-$2F

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $20  | (sp) | U+0020  | Space                   | ASCII    |
| $21  | !    | U+0021  | Exclamation mark        | ASCII    |
| $22  | "    | U+0022  | Quotation mark          | ASCII    |
| $23  | #    | U+0023  | Number sign             | ASCII    |
| $24  | $    | U+0024  | Dollar sign             | ASCII    |
| $25  | %    | U+0025  | Percent sign            | ASCII    |
| $26  | &    | U+0026  | Ampersand               | ASCII    |
| $27  | '    | U+0027  | Apostrophe              | ASCII    |
| $28  | (    | U+0028  | Left parenthesis        | ASCII    |
| $29  | )    | U+0029  | Right parenthesis       | ASCII    |
| $2A  | *    | U+002A  | Asterisk                | ASCII    |
| $2B  | +    | U+002B  | Plus sign               | ASCII    |
| $2C  | ,    | U+002C  | Comma                   | ASCII    |
| $2D  | -    | U+002D  | Hyphen-minus            | ASCII    |
| $2E  | .    | U+002E  | Full stop               | ASCII    |
| $2F  | /    | U+002F  | Solidus                 | ASCII    |

## Row $3x: ASCII $30-$3F

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $30  | 0    | U+0030  | Digit zero              | ASCII    |
| $31  | 1    | U+0031  | Digit one               | ASCII    |
| $32  | 2    | U+0032  | Digit two               | ASCII    |
| $33  | 3    | U+0033  | Digit three             | ASCII    |
| $34  | 4    | U+0034  | Digit four              | ASCII    |
| $35  | 5    | U+0035  | Digit five              | ASCII    |
| $36  | 6    | U+0036  | Digit six               | ASCII    |
| $37  | 7    | U+0037  | Digit seven             | ASCII    |
| $38  | 8    | U+0038  | Digit eight             | ASCII    |
| $39  | 9    | U+0039  | Digit nine              | ASCII    |
| $3A  | :    | U+003A  | Colon                   | ASCII    |
| $3B  | ;    | U+003B  | Semicolon               | ASCII    |
| $3C  | <    | U+003C  | Less-than sign          | ASCII    |
| $3D  | =    | U+003D  | Equals sign             | ASCII    |
| $3E  | >    | U+003E  | Greater-than sign       | ASCII    |
| $3F  | ?    | U+003F  | Question mark           | ASCII    |

## Row $4x: ASCII $40-$4F

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $40  | @    | U+0040  | Commercial at           | ASCII    |
| $41  | A    | U+0041  | Latin capital letter A   | ASCII    |
| $42  | B    | U+0042  | Latin capital letter B   | ASCII    |
| $43  | C    | U+0043  | Latin capital letter C   | ASCII    |
| $44  | D    | U+0044  | Latin capital letter D   | ASCII    |
| $45  | E    | U+0045  | Latin capital letter E   | ASCII    |
| $46  | F    | U+0046  | Latin capital letter F   | ASCII    |
| $47  | G    | U+0047  | Latin capital letter G   | ASCII    |
| $48  | H    | U+0048  | Latin capital letter H   | ASCII    |
| $49  | I    | U+0049  | Latin capital letter I   | ASCII    |
| $4A  | J    | U+004A  | Latin capital letter J   | ASCII    |
| $4B  | K    | U+004B  | Latin capital letter K   | ASCII    |
| $4C  | L    | U+004C  | Latin capital letter L   | ASCII    |
| $4D  | M    | U+004D  | Latin capital letter M   | ASCII    |
| $4E  | N    | U+004E  | Latin capital letter N   | ASCII    |
| $4F  | O    | U+004F  | Latin capital letter O   | ASCII    |

## Row $5x: ASCII $50-$5F

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $50  | P    | U+0050  | Latin capital letter P   | ASCII    |
| $51  | Q    | U+0051  | Latin capital letter Q   | ASCII    |
| $52  | R    | U+0052  | Latin capital letter R   | ASCII    |
| $53  | S    | U+0053  | Latin capital letter S   | ASCII    |
| $54  | T    | U+0054  | Latin capital letter T   | ASCII    |
| $55  | U    | U+0055  | Latin capital letter U   | ASCII    |
| $56  | V    | U+0056  | Latin capital letter V   | ASCII    |
| $57  | W    | U+0057  | Latin capital letter W   | ASCII    |
| $58  | X    | U+0058  | Latin capital letter X   | ASCII    |
| $59  | Y    | U+0059  | Latin capital letter Y   | ASCII    |
| $5A  | Z    | U+005A  | Latin capital letter Z   | ASCII    |
| $5B  | [    | U+005B  | Left square bracket     | ASCII    |
| $5C  | \    | U+005C  | Reverse solidus         | ASCII    |
| $5D  | ]    | U+005D  | Right square bracket    | ASCII    |
| $5E  | ^    | U+005E  | Circumflex accent       | ASCII    |
| $5F  | _    | U+005F  | Low line                | ASCII    |

## Row $6x: ASCII $60-$6F

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $60  | `    | U+0060  | Grave accent            | ASCII    |
| $61  | a    | U+0061  | Latin small letter a    | ASCII    |
| $62  | b    | U+0062  | Latin small letter b    | ASCII    |
| $63  | c    | U+0063  | Latin small letter c    | ASCII    |
| $64  | d    | U+0064  | Latin small letter d    | ASCII    |
| $65  | e    | U+0065  | Latin small letter e    | ASCII    |
| $66  | f    | U+0066  | Latin small letter f    | ASCII    |
| $67  | g    | U+0067  | Latin small letter g    | ASCII    |
| $68  | h    | U+0068  | Latin small letter h    | ASCII    |
| $69  | i    | U+0069  | Latin small letter i    | ASCII    |
| $6A  | j    | U+006A  | Latin small letter j    | ASCII    |
| $6B  | k    | U+006B  | Latin small letter k    | ASCII    |
| $6C  | l    | U+006C  | Latin small letter l    | ASCII    |
| $6D  | m    | U+006D  | Latin small letter m    | ASCII    |
| $6E  | n    | U+006E  | Latin small letter n    | ASCII    |
| $6F  | o    | U+006F  | Latin small letter o    | ASCII    |

## Row $7x: ASCII $70-$7E + House at $7F

| Code | Char | Unicode | Name                    | Category |
|------|------|---------|-------------------------|----------|
| $70  | p    | U+0070  | Latin small letter p    | ASCII    |
| $71  | q    | U+0071  | Latin small letter q    | ASCII    |
| $72  | r    | U+0072  | Latin small letter r    | ASCII    |
| $73  | s    | U+0073  | Latin small letter s    | ASCII    |
| $74  | t    | U+0074  | Latin small letter t    | ASCII    |
| $75  | u    | U+0075  | Latin small letter u    | ASCII    |
| $76  | v    | U+0076  | Latin small letter v    | ASCII    |
| $77  | w    | U+0077  | Latin small letter w    | ASCII    |
| $78  | x    | U+0078  | Latin small letter x    | ASCII    |
| $79  | y    | U+0079  | Latin small letter y    | ASCII    |
| $7A  | z    | U+007A  | Latin small letter z    | ASCII    |
| $7B  | {    | U+007B  | Left curly bracket      | ASCII    |
| $7C  | \|   | U+007C  | Vertical line           | ASCII    |
| $7D  | }    | U+007D  | Right curly bracket     | ASCII    |
| $7E  | ~    | U+007E  | Tilde                   | ASCII    |
| $7F  | ⌐    | U+2310  | Reversed not sign       | SYMBOL   |

## Row $8x: Block Elements — Half Blocks, Quadrants

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $80  | █    | U+2588  | Full block                   | BLOCK    |
| $81  | ▀    | U+2580  | Upper half block             | BLOCK    |
| $82  | ▄    | U+2584  | Lower half block             | BLOCK    |
| $83  | ▌    | U+258C  | Left half block              | BLOCK    |
| $84  | ▐    | U+2590  | Right half block             | BLOCK    |
| $85  | ▘    | U+2598  | Quadrant upper left          | BLOCK    |
| $86  | ▝    | U+259D  | Quadrant upper right         | BLOCK    |
| $87  | ▖    | U+2596  | Quadrant lower left          | BLOCK    |
| $88  | ▗    | U+2597  | Quadrant lower right         | BLOCK    |
| $89  | ▚    | U+259A  | Quadrant upper left + lower right | BLOCK |
| $8A  | ▞    | U+259E  | Quadrant upper right + lower left | BLOCK |
| $8B  | ▛    | U+259B  | Quadrant upper left + upper right + lower left  | BLOCK |
| $8C  | ▜    | U+259C  | Quadrant upper left + upper right + lower right | BLOCK |
| $8D  | ▙    | U+2599  | Quadrant upper left + lower left + lower right  | BLOCK |
| $8E  | ▟    | U+259F  | Quadrant upper right + lower left + lower right | BLOCK |
| $8F  | ░    | U+2591  | Light shade                  | BLOCK    |

## Row $9x: Block Elements — Shading, Thirds, Diagonals

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $90  | ▒    | U+2592  | Medium shade                 | BLOCK    |
| $91  | ▓    | U+2593  | Dark shade                   | BLOCK    |
| $92  | ---  | (N8)    | Upper one-third block        | BLOCK    |
| $93  | ---  | (N8)    | Lower one-third block        | BLOCK    |
| $94  | ---  | (N8)    | Left one-third block         | BLOCK    |
| $95  | ---  | (N8)    | Right one-third block        | BLOCK    |
| $96  | ---  | (N8)    | Upper two-thirds block       | BLOCK    |
| $97  | ---  | (N8)    | Lower two-thirds block       | BLOCK    |
| $98  | ◢    | U+25E2  | Black lower right triangle (diagonal) | BLOCK |
| $99  | ◣    | U+25E3  | Black lower left triangle (diagonal)  | BLOCK |
| $9A  | ◤    | U+25E4  | Black upper left triangle (diagonal)  | BLOCK |
| $9B  | ◥    | U+25E5  | Black upper right triangle (diagonal) | BLOCK |
| $9C  | ╱    | U+2571  | Box drawings light diagonal upper right to lower left | BLOCK |
| $9D  | ╲    | U+2572  | Box drawings light diagonal upper left to lower right | BLOCK |
| $9E  | ╳    | U+2573  | Box drawings light diagonal cross | BLOCK |
| $9F  | ▬    | U+25AC  | Black rectangle (horizontal bar) | BLOCK |

Entries marked (N8) are N8-specific characters with no standard Unicode equivalent. The one-third and two-thirds blocks subdivide the 8x16 cell into horizontal or vertical thirds — a natural fit for the 16-row height (5+6+5 rows) but not part of any Unicode block.

## Row $Ax: Box Drawing — Single Line

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $A0  | ─    | U+2500  | Light horizontal             | BOX      |
| $A1  | │    | U+2502  | Light vertical               | BOX      |
| $A2  | ┌    | U+250C  | Light down and right          | BOX      |
| $A3  | ┐    | U+2510  | Light down and left           | BOX      |
| $A4  | └    | U+2514  | Light up and right            | BOX      |
| $A5  | ┘    | U+2518  | Light up and left             | BOX      |
| $A6  | ├    | U+251C  | Light vertical and right      | BOX      |
| $A7  | ┤    | U+2524  | Light vertical and left       | BOX      |
| $A8  | ┬    | U+252C  | Light down and horizontal     | BOX      |
| $A9  | ┴    | U+2534  | Light up and horizontal       | BOX      |
| $AA  | ┼    | U+253C  | Light vertical and horizontal | BOX      |

## Row $Ax (cont'd): Box Drawing — Heavy Line

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $AB  | ━    | U+2501  | Heavy horizontal             | BOX      |
| $AC  | ┃    | U+2503  | Heavy vertical               | BOX      |
| $AD  | ┏    | U+250F  | Heavy down and right          | BOX      |
| $AE  | ┓    | U+2513  | Heavy down and left           | BOX      |
| $AF  | ┗    | U+2517  | Heavy up and right            | BOX      |

## Row $Bx: Box Drawing — Heavy Line (cont'd), Rounded Corners, Extras

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $B0  | ┛    | U+251B  | Heavy up and left            | BOX      |
| $B1  | ┣    | U+2523  | Heavy vertical and right     | BOX      |
| $B2  | ┫    | U+252B  | Heavy vertical and left      | BOX      |
| $B3  | ┳    | U+2533  | Heavy down and horizontal    | BOX      |
| $B4  | ┻    | U+253B  | Heavy up and horizontal      | BOX      |
| $B5  | ╋    | U+254B  | Heavy vertical and horizontal | BOX     |

### Rounded Box Corners

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $B6  | ╭    | U+256D  | Light arc down and right     | BOX      |
| $B7  | ╮    | U+256E  | Light arc down and left      | BOX      |
| $B8  | ╰    | U+2570  | Light arc up and right       | BOX      |
| $B9  | ╯    | U+256F  | Light arc up and left        | BOX      |

### Box Drawing Extras

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $BA  | ╌    | U+254C  | Light double-dash horizontal | BOX      |
| $BB  | ╎    | U+254E  | Light double-dash vertical   | BOX      |
| $BC  | ╍    | U+254D  | Heavy double-dash horizontal | BOX      |
| $BD  | ╏    | U+254F  | Heavy double-dash vertical   | BOX      |

### International Characters (start)

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $BE  | ¡    | U+00A1  | Inverted exclamation mark    | INTL     |
| $BF  | ¿    | U+00BF  | Inverted question mark       | INTL     |

## Row $Cx: International / Accented Characters

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $C0  | À    | U+00C0  | Latin capital A with grave   | INTL     |
| $C1  | Á    | U+00C1  | Latin capital A with acute   | INTL     |
| $C2  | Ä    | U+00C4  | Latin capital A with diaeresis | INTL   |
| $C3  | Ç    | U+00C7  | Latin capital C with cedilla | INTL     |
| $C4  | É    | U+00C9  | Latin capital E with acute   | INTL     |
| $C5  | Ñ    | U+00D1  | Latin capital N with tilde   | INTL     |
| $C6  | Ö    | U+00D6  | Latin capital O with diaeresis | INTL   |
| $C7  | Ü    | U+00DC  | Latin capital U with diaeresis | INTL   |
| $C8  | à    | U+00E0  | Latin small a with grave     | INTL     |
| $C9  | á    | U+00E1  | Latin small a with acute     | INTL     |
| $CA  | ä    | U+00E4  | Latin small a with diaeresis | INTL     |
| $CB  | ç    | U+00E7  | Latin small c with cedilla   | INTL     |
| $CC  | é    | U+00E9  | Latin small e with acute     | INTL     |
| $CD  | ñ    | U+00F1  | Latin small n with tilde     | INTL     |
| $CE  | ö    | U+00F6  | Latin small o with diaeresis | INTL     |
| $CF  | ü    | U+00FC  | Latin small u with diaeresis | INTL     |

## Row $Dx: Geometric Shapes, Card Suits, Dice

| Code | Char | Unicode | Name                           | Category |
|------|------|---------|--------------------------------|----------|
| $D0  | ○    | U+25CB  | White circle                   | GEOM     |
| $D1  | ◎    | U+25CE  | Bullseye                       | GEOM     |
| $D2  | □    | U+25A1  | White square                   | GEOM     |
| $D3  | ■    | U+25A0  | Black square                   | GEOM     |
| $D4  | △    | U+25B3  | White up-pointing triangle     | GEOM     |
| $D5  | ▷    | U+25B7  | White right-pointing triangle  | GEOM     |
| $D6  | ▽    | U+25BD  | White down-pointing triangle   | GEOM     |
| $D7  | ◁    | U+25C1  | White left-pointing triangle   | GEOM     |
| $D8  | ◇    | U+25C7  | White diamond                  | GEOM     |
| $D9  | ☆    | U+2606  | White star                     | GEOM     |

### Dice Faces

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $DA  | ⚀    | U+2680  | Die face 1                   | SUIT     |
| $DB  | ⚁    | U+2681  | Die face 2                   | SUIT     |
| $DC  | ⚂    | U+2682  | Die face 3                   | SUIT     |
| $DD  | ⚃    | U+2683  | Die face 4                   | SUIT     |
| $DE  | ⚄    | U+2684  | Die face 5                   | SUIT     |
| $DF  | ⚅    | U+2685  | Die face 6                   | SUIT     |

## Row $Ex: Mathematical / Greek Symbols

| Code | Char | Unicode | Name                        | Category |
|------|------|---------|------------------------------|----------|
| $E0  | ±    | U+00B1  | Plus-minus sign              | MATH     |
| $E1  | ×    | U+00D7  | Multiplication sign          | MATH     |
| $E2  | ÷    | U+00F7  | Division sign                | MATH     |
| $E3  | ≠    | U+2260  | Not equal to                 | MATH     |
| $E4  | ≤    | U+2264  | Less-than or equal to        | MATH     |
| $E5  | ≥    | U+2265  | Greater-than or equal to     | MATH     |
| $E6  | ≈    | U+2248  | Almost equal to              | MATH     |
| $E7  | °    | U+00B0  | Degree sign                  | MATH     |
| $E8  | ∞    | U+221E  | Infinity                     | MATH     |
| $E9  | √    | U+221A  | Square root                  | MATH     |
| $EA  | π    | U+03C0  | Greek small letter pi        | MATH     |
| $EB  | Σ    | U+03A3  | Greek capital letter sigma   | MATH     |
| $EC  | σ    | U+03C3  | Greek small letter sigma     | MATH     |
| $ED  | μ    | U+03BC  | Greek small letter mu        | MATH     |
| $EE  | Ω    | U+03A9  | Greek capital letter omega   | MATH     |
| $EF  | δ    | U+03B4  | Greek small letter delta     | MATH     |

## Row $Fx: Currency, Electronics, Decorative, Remaining Symbols

| Code | Char | Unicode | Name                           | Category    |
|------|------|---------|--------------------------------|-------------|
| $F0  | ¢    | U+00A2  | Cent sign                      | CURRENCY    |
| $F1  | £    | U+00A3  | Pound sign                     | CURRENCY    |
| $F2  | ¥    | U+00A5  | Yen sign                       | CURRENCY    |
| $F3  | €    | U+20AC  | Euro sign                      | CURRENCY    |
| $F4  | ¤    | U+00A4  | Currency sign (generic)        | CURRENCY    |
| $F5  | ⏻    | U+23FB  | Power symbol                   | ELECTRONICS |
| $F6  | ⏚    | U+23DA  | Earth ground                   | ELECTRONICS |
| $F7  | ⎍    | U+238D  | Monostable / pulse             | ELECTRONICS |
| $F8  | ⌖    | U+2316  | Crosshair / position indicator | ELECTRONICS |
| $F9  | ⌘    | U+2318  | Command / place of interest    | ELECTRONICS |
| $FA  | ©    | U+00A9  | Copyright sign                 | SYMBOL      |
| $FB  | ®    | U+00AE  | Registered sign                | SYMBOL      |
| $FC  | «    | U+00AB  | Left guillemet                 | DECORATIVE  |
| $FD  | »    | U+00BB  | Right guillemet                | DECORATIVE  |
| $FE  | ‹    | U+2039  | Single left guillemet          | DECORATIVE  |
| $FF  | ›    | U+203A  | Single right guillemet         | DECORATIVE  |

---

## 16x16 Quick Reference Grid

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

---

## Category Summary

| Category    | Code                | Count | Description                                |
|-------------|---------------------|-------|--------------------------------------------|
| ASCII       | $20-$7E             | 95    | Standard printable ASCII                   |
| ARROW       | $0C-$0F, $10-$1A   | 19    | Cardinal, double, triangular, bidirectional|
| BLOCK       | $80-$9F             | 32    | Full/half/quarter blocks, shading, thirds, diagonals |
| BOX         | $A0-$BD             | 30    | Single-line, heavy-line, rounded, dashed   |
| INTL        | $BE-$CF             | 18    | Accented/international characters          |
| GEOM        | $02, $07, $0A-$0B, $D0-$D9 | 14 | Circles, squares, triangles, diamonds, stars |
| SUIT        | $03-$06, $DA-$DF    | 10    | Card suits + dice faces                    |
| MATH        | $E0-$EF             | 16    | Math operators, Greek letters              |
| SYMBOL      | $00-$01, $08-$09, $1B-$1F, $7F, $FA-$FB | 12 | Smiley, check/X, musical, section, etc. |
| CURRENCY    | $F0-$F4             | 5     | Cent, pound, yen, euro, generic            |
| ELECTRONICS | $F5-$F9             | 5     | Power, ground, pulse, crosshair, command   |
| DECORATIVE  | $FC-$FF             | 4     | Guillemets (double and single)             |
| **TOTAL**   |                     | **256** |                                          |

---

## Detailed Category Counts vs. Budget

| Category       | Budget | Final | Delta | Notes                                    |
|----------------|--------|-------|-------|------------------------------------------|
| ASCII          | 95     | 95    | 0     | Fixed, $20-$7E                           |
| Arrows         | ~16    | 19    | +3    | Added return, bidirectional arrows        |
| Block/Mosaic   | ~20    | 32    | +12   | Full quadrant set, thirds, diagonals, bar|
| Box Drawing    | ~22    | 30    | +8    | Added rounded corners + dashed variants  |
| International  | ~16    | 18    | +2    | 8 uppercase + 8 lowercase + inverted punctuation |
| Geometric      | ~14    | 14    | 0     | On target                                |
| Card Suits+Dice| ~10    | 10    | 0     | On target                                |
| Math/Greek     | ~16    | 16    | 0     | On target                                |
| Symbols        | ~18    | 12    | -6    | Trimmed — moved house to $1B, consolidated |
| Currency       | ~5     | 5     | 0     | On target                                |
| Electronics    | ~8     | 5     | -3    | Trimmed to most recognizable at 8x16     |
| Decorative     | ~8     | 4     | -4    | Guillemets only — best use of pixels     |
| Reserve/Spare  | ~4     | 0     | -4    | All slots filled — no waste              |
| **TOTAL**      | **256**| **256** | **0** |                                        |

---

## Design Rationale

### $00-$1F Placement
The first 32 positions hold the most frequently needed graphic characters:
- **$03-$06**: Card suits grouped together (classic game programming pattern)
- **$07-$09**: Bullet, check, X — the three most common list/status markers
- **$0C-$0F**: Cardinal arrows — the most common directional indicators
- **$15-$18**: Triangular arrows — useful as menu pointers, media controls
- **$1B**: House symbol — useful "home" indicator, nod to CP437 tradition

### $80-$9F Block Elements
A complete set for pseudo-graphics. The full quadrant set ($85-$8E, all 10 non-trivial quadrant combinations) enables 2x2 "pixel" subdivision of any character cell. The diagonal triangles ($98-$9B) and diagonal lines ($9C-$9E) enable smooth diagonal edges in graphics.

### $A0-$BD Box Drawing
The most generous box drawing allocation of any 8-bit charset:
- 11 single-line pieces ($A0-$AA): complete set including cross
- 11 heavy-line pieces ($AB-$B5): complete set including cross
- 4 rounded corners ($B6-$B9): for friendly/modern UI elements
- 4 dashed lines ($BA-$BD): for separators, guides, cut-lines

### $C0-$CF International
Focused on the most commonly needed Western European accented characters. The selection covers Spanish, French, German, and Portuguese — the languages most likely encountered by the N8's target audience. Uppercase at $C0-$C7, lowercase at $C8-$CF for easy case conversion (XOR $08).

### $7F
The reversed not sign (⌐) was chosen over the more traditional DEL-replacement candidates. It pairs with the standard not sign (¬, absent but similar to ~ at $7E), and is useful in logic displays. The house symbol was moved to $1B where it's more accessible.

### Electronics Characters ($F5-$F9)
Trimmed from the original 8 to the 5 most recognizable at 8x16 resolution. Power symbol, ground, pulse waveform, crosshair, and the command/place-of-interest symbol. The ohm symbol is redundant with Greek omega (Ω at $EE). The antenna symbol was cut as it's too similar to an arrow at low resolution.
