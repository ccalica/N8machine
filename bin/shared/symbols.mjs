/**
 * Symbol table management for cc65 .sym files.
 */

import { readFileSync } from 'fs';

/**
 * Load a cc65 .sym file into symbol/address maps.
 * @param {string} path - Path to .sym file
 * @param {Map<string, number>} symbols - name -> addr map to populate
 * @param {Map<number, string[]>} addrLabels - addr -> name[] map to populate
 * @returns {number} Number of symbols loaded
 */
export function loadSymbols(path, symbols, addrLabels) {
  const content = readFileSync(path, 'utf8');
  let count = 0;
  for (const line of content.split(/\r?\n/)) {
    const m = line.match(/^al\s+([0-9a-fA-F]+)\s+\.(\S+)/);
    if (m) {
      const addr = parseInt(m[1], 16);
      const name = m[2];
      symbols.set(name, addr);
      if (!addrLabels.has(addr)) addrLabels.set(addr, []);
      addrLabels.get(addr).push(name);
      count++;
    }
  }
  return count;
}
