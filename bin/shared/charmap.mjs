/**
 * N8 character map — maps byte values ($00-$FF) to Unicode characters.
 */

// prettier-ignore
export const N8_CHARMAP = [
  // $00-$0F
  ' ',     '\u263A', '\u25CF', '\u2665', '\u2666', '\u2663', '\u2660', '\u2026',
  '\u2713', '\u2717', '\u2605', '\u00DF', '\u2190', '\u2192', '\u2191', '\u2193',
  // $10-$1F
  '\u21B5', '\u21D0', '\u21D2', '\u21D1', '\u21D3', '\u25B6', '\u25C0', '\u25B2',
  '\u25BC', '\u2194', '\u2195', '\u2302', '\u266A', '\u266B', '\u00A7', '\u00B6',
  // $20-$7E: standard ASCII
  ...Array.from({length: 95}, (_, i) => String.fromCharCode(0x20 + i)),
  // $7F
  '\u2310',
  // $80-$8F: block elements
  '\u2588', '\u2580', '\u2584', '\u258C', '\u2590', '\u2598', '\u259D', '\u2596',
  '\u2597', '\u259A', '\u259E', '\u259B', '\u259C', '\u2599', '\u259F', '\u2591',
  // $90-$9F: shading, thirds, diagonals
  '\u2592', '\u2593', '\u2594', '\u2581', '\u258F', '\u2595', '\u2586', '\u2582',
  '\u25E2', '\u25E3', '\u25E4', '\u25E5', '\u2571', '\u2572', '\u2573', '\u25AC',
  // $A0-$AA: single-line box drawing
  '\u2500', '\u2502', '\u250C', '\u2510', '\u2514', '\u2518', '\u251C', '\u2524',
  '\u252C', '\u2534', '\u253C',
  // $AB-$B5: heavy-line box drawing
  '\u2501', '\u2503', '\u250F', '\u2513', '\u2517', '\u251B', '\u2523', '\u252B',
  '\u2533', '\u253B', '\u254B',
  // $B6-$B9: rounded corners
  '\u256D', '\u256E', '\u2570', '\u256F',
  // $BA-$BD: dashed
  '\u254C', '\u254E', '\u254D', '\u254F',
  // $BE-$BF: inverted punctuation
  '\u00A1', '\u00BF',
  // $C0-$CF: international
  '\u00C0', '\u00C1', '\u00C4', '\u00C7', '\u00C9', '\u00D1', '\u00D6', '\u00DC',
  '\u00E0', '\u00E1', '\u00E4', '\u00E7', '\u00E9', '\u00F1', '\u00F6', '\u00FC',
  // $D0-$D9: geometric
  '\u25CB', '\u25CE', '\u25A1', '\u25A0', '\u25B3', '\u25B7', '\u25BD', '\u25C1',
  '\u25C7', '\u2606',
  // $DA-$DF: dice
  '\u2680', '\u2681', '\u2682', '\u2683', '\u2684', '\u2685',
  // $E0-$EF: math/greek
  '\u00B1', '\u00D7', '\u00F7', '\u2260', '\u2264', '\u2265', '\u2248', '\u00B0',
  '\u221E', '\u221A', '\u03C0', '\u03A3', '\u03C3', '\u03BC', '\u03A9', '\u03B4',
  // $F0-$F4: currency
  '\u00A2', '\u00A3', '\u00A5', '\u20AC', '\u00A4',
  // $F5-$F9: electronics
  '\u23FB', '\u23DA', '\u26A1', '\u2316', '\u2318',
  // $FA-$FB: copyright/registered
  '\u00A9', '\u00AE',
  // $FC-$FD: guillemets
  '\u00AB', '\u00BB',
  // $FE-$FF: N8 two-thirds blocks (closest Unicode approx)
  '\u258A', '\u258E',
];
