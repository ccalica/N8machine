/**
 * Formatting utilities for N8machine tools.
 */

export function hex8(v) { return v.toString(16).padStart(2, '0'); }
export function hex16(v) { return v.toString(16).padStart(4, '0'); }

function bit(v, b) { return (v >> b) & 1; }

/**
 * Format a hex dump of a buffer.
 * @param {Buffer|Uint8Array} buf
 * @param {number} baseAddr
 * @returns {string}
 */
export function hexdump(buf, baseAddr) {
  const lines = [];
  for (let off = 0; off < buf.length; off += 16) {
    const slice = buf.subarray(off, Math.min(off + 16, buf.length));
    const hex = Array.from(slice).map(b => b.toString(16).padStart(2, '0')).join(' ');
    const ascii = Array.from(slice).map(b => (b >= 0x20 && b <= 0x7e) ? String.fromCharCode(b) : '.').join('');
    const addr = (baseAddr + off).toString(16).padStart(4, '0');
    lines.push(`${addr}: ${hex.padEnd(48)} ${ascii}`);
  }
  return lines.join('\n');
}

/**
 * Format CPU registers.
 * @param {{ a: number, x: number, y: number, s: number, p: number, pc: number }} r
 * @param {Map<number, string[]>} [addrLabels] - Optional address-to-label map
 * @returns {string}
 */
export function fmtRegs(r, addrLabels) {
  const lines = [];
  lines.push(`A:${hex8(r.a)}  X:${hex8(r.x)}  Y:${hex8(r.y)}  S:${hex8(r.s)}  P:${hex8(r.p)}  PC:${hex16(r.pc)}`);
  const p = r.p;
  const flags = `N${bit(p,7)} V${bit(p,6)} -${bit(p,5)} B${bit(p,4)} D${bit(p,3)} I${bit(p,2)} Z${bit(p,1)} C${bit(p,0)}`;
  lines.push(`Flags: ${flags}`);
  if (addrLabels?.has(r.pc)) lines.push(`  @ ${addrLabels.get(r.pc).join(', ')}`);
  return lines.join('\n');
}

/**
 * Format a stop reply from GDB RSP.
 * @param {string} reply
 * @returns {string}
 */
export function fmtStop(reply) {
  if (!reply) return 'no reply';
  if (reply.startsWith('T05')) {
    if (reply.includes('awatch:')) return `watchpoint (access) hit`;
    if (reply.includes('rwatch:')) return `watchpoint (read) hit`;
    if (reply.includes('watch:')) return `watchpoint (write) hit`;
    return `breakpoint hit`;
  }
  if (reply.startsWith('T')) return `stopped signal ${parseInt(reply.slice(1, 3), 16)}`;
  if (reply.startsWith('S')) return `stopped signal ${parseInt(reply.slice(1, 3), 16)}`;
  return reply;
}
