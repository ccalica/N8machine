/**
 * Address parsing for N8machine tools.
 * Supports hex (0x, $, bare), decimal (#), and label lookup.
 */

/**
 * Parse an address string to a number.
 * @param {string} str - Address string
 * @param {Map<string, number>} [symbols] - Optional symbol table for label lookup
 * @returns {number} Parsed address, or NaN if invalid
 */
export function parseAddr(str, symbols) {
  if (!str) return NaN;
  if (symbols?.has(str)) return symbols.get(str);
  if (str.startsWith('#')) return parseInt(str.slice(1), 10);
  if (str.startsWith('0x') || str.startsWith('0X')) return parseInt(str.slice(2), 16);
  if (str.startsWith('$')) return parseInt(str.slice(1), 16);
  if (/[g-zG-Z_]/.test(str)) return NaN;
  return parseInt(str, 16);
}
