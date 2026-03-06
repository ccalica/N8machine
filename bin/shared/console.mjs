/**
 * Console text reading for N8machine video framebuffer.
 * Shared by n8mcp and n8gdb.
 */

import { N8_CHARMAP } from './charmap.mjs';
import { hex8 } from './format.mjs';

/**
 * Read video registers and framebuffer, return formatted Unicode text.
 * @param {object} rsp - RspClient instance with readMemory()
 * @returns {Promise<string>}
 */
export async function readConsoleText(rsp) {
  const regs = await rsp.readMemory(0xD840, 12);
  const mode   = regs[0];
  const width  = regs[1];
  const height = regs[2];
  const stride = regs[3];
  const curStyle = regs[5];
  const curCol   = regs[6];
  const curRow   = regs[7];

  const fbSize = stride * height;
  if (fbSize === 0 || fbSize > 0x1000) {
    throw new Error(`Invalid video dimensions: ${width}x${height} stride=${stride}`);
  }

  const fb = await rsp.readMemory(0xC000, fbSize);

  const modeStr = mode === 0 ? 'Text Default' : mode === 1 ? 'Text Custom' : `0x${hex8(mode)}`;
  const curModeStr = (curStyle & 0x03) === 0 ? 'off' : (curStyle & 0x03) === 1 ? 'steady' : 'flash';
  const curShapeStr = (curStyle & 0x0C) === 0x04 ? 'block' : 'underline';

  const lines = [];
  lines.push(`Mode: ${modeStr}  Size: ${width}\u00D7${height}  Stride: ${stride}  Cursor: (${curCol},${curRow}) ${curModeStr} ${curShapeStr}`);
  lines.push('\u2500'.repeat(width));

  for (let row = 0; row < height; row++) {
    let line = '';
    for (let col = 0; col < width; col++) {
      const byte = fb[row * stride + col];
      line += N8_CHARMAP[byte] || '?';
    }
    lines.push(line);
  }
  lines.push('\u2500'.repeat(width));
  return lines.join('\n');
}
