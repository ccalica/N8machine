#!/usr/bin/env node

/**
 * n8gdb — GDB RSP client for N8machine 6502 emulator.
 *
 * Usage:
 *   n8gdb [--host HOST] [--port PORT] <command> [args...]
 *   n8gdb repl                    Interactive REPL
 *
 * Commands:
 *   regs                          Read all CPU registers
 *   wreg  <name> <val>            Write register (a x y s p pc)
 *   pc    <addr|label>            Set PC (safe — sets SYNC state)
 *   goto  <addr|label>            Set PC and continue execution
 *   read  <addr> [len]            Read memory (hex dump)
 *   write <addr> <hex|@file>      Write hex bytes or load file to address
 *   load  <file> <addr>           Load binary file at address
 *   sym   <file>                  Load cc65 .sym file, print labels
 *   bp    <addr|label>            Set breakpoint
 *   bpc   <addr|label>            Clear breakpoint
 *   run   [--timeout ms]          Continue execution (wait for stop)
 *   step  [n]                     Single step (n times)
 *   halt                          Send interrupt (Ctrl-C)
 *   reset                         Reset CPU
 *   detach                        Detach and disconnect
 *   repl                          Interactive REPL mode
 *
 * Addresses: hex with 0x or $ prefix, or bare hex. Decimal with # prefix.
 * Labels: if a .sym file is loaded (--sym), label names resolve to addresses.
 *
 * Environment:
 *   N8GDB_HOST    Default host (127.0.0.1)
 *   N8GDB_PORT    Default port (3333)
 *   N8GDB_SYM     Default .sym file path
 *   N8GDB_DEBUG   Set to 1 for RSP debug logging
 *
 * Exit codes:
 *   0  Success
 *   1  Error
 *   2  Usage error
 */

import { RspClient } from './rsp.mjs';
import { readFileSync, writeFileSync, existsSync } from 'fs';
import { createInterface } from 'readline';

// ── Symbol table ────────────────────────────────────────────────

const symbols = new Map();  // name -> addr
const addrLabels = new Map();  // addr -> name[]

function loadSymbols(path) {
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

// ── Address parsing ─────────────────────────────────────────────

function parseAddr(str) {
  if (!str) return NaN;
  // Label lookup
  if (symbols.has(str)) return symbols.get(str);
  // Decimal with # prefix
  if (str.startsWith('#')) return parseInt(str.slice(1), 10);
  // Hex with prefix
  if (str.startsWith('0x') || str.startsWith('0X')) return parseInt(str.slice(2), 16);
  if (str.startsWith('$')) return parseInt(str.slice(1), 16);
  // If it contains non-hex chars (without a prefix), it's likely a mistyped label
  if (/[g-zG-Z_]/.test(str)) return NaN;
  // Bare hex
  return parseInt(str, 16);
}

// ── Output helpers ──────────────────────────────────────────────

function hexdump(buf, baseAddr) {
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

function fmtRegs(r) {
  const lines = [];
  lines.push(`A:${hex8(r.a)}  X:${hex8(r.x)}  Y:${hex8(r.y)}  S:${hex8(r.s)}  P:${hex8(r.p)}  PC:${hex16(r.pc)}`);
  const p = r.p;
  const flags = `N${b(p,7)} V${b(p,6)} -${b(p,5)} B${b(p,4)} D${b(p,3)} I${b(p,2)} Z${b(p,1)} C${b(p,0)}`;
  lines.push(`Flags: ${flags}`);
  // Label at PC
  if (addrLabels.has(r.pc)) lines.push(`  @ ${addrLabels.get(r.pc).join(', ')}`);
  return lines.join('\n');
}

function hex8(v) { return v.toString(16).padStart(2, '0'); }
function hex16(v) { return v.toString(16).padStart(4, '0'); }
function b(v, bit) { return (v >> bit) & 1; }

function fmtStop(reply) {
  if (!reply) return 'no reply';
  if (reply.startsWith('T05')) {
    if (reply.includes('watch:')) return `watchpoint (write) hit — ${reply}`;
    if (reply.includes('rwatch:')) return `watchpoint (read) hit — ${reply}`;
    if (reply.includes('awatch:')) return `watchpoint (access) hit — ${reply}`;
    return `breakpoint hit — ${reply}`;
  }
  if (reply.startsWith('T')) return `stopped signal ${parseInt(reply.slice(1, 3), 16)} — ${reply}`;
  if (reply.startsWith('S')) return `stopped signal ${parseInt(reply.slice(1, 3), 16)}`;
  return reply;
}

// ── Command implementations ─────────────────────────────────────

async function cmdRegs(client) {
  const r = await client.readRegisters();
  console.log(fmtRegs(r));
}

async function cmdRead(client, args) {
  const addr = parseAddr(args[0]);
  const len = args[1] ? parseInt(args[1], 10) : 16;
  if (isNaN(addr)) { console.error('Usage: read <addr> [len]'); return; }
  const buf = await client.readMemory(addr, len);
  console.log(hexdump(buf, addr));
}

async function cmdWrite(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: write <addr> <hex|@file>'); return; }
  const src = args[1];
  if (!src) { console.error('Usage: write <addr> <hex|@file>'); return; }
  let data;
  if (src.startsWith('@')) {
    data = readFileSync(src.slice(1));
  } else {
    const clean = src.replace(/[\s,]/g, '');
    if (clean.length === 0) { console.error('Error: empty hex string'); return; }
    if (clean.length % 2 !== 0) { console.error('Error: hex string must have even number of digits'); return; }
    if (!/^[0-9a-fA-F]+$/.test(clean)) { console.error('Error: invalid hex characters'); return; }
    data = Buffer.from(clean, 'hex');
  }
  await client.writeMemory(addr, data);
  console.log(`Wrote ${data.length} bytes at $${hex16(addr)}`);
}

async function cmdLoad(client, args) {
  const file = args[0];
  const addr = parseAddr(args[1]);
  if (!file || isNaN(addr)) { console.error('Usage: load <file> <addr>'); return; }
  const data = readFileSync(file);
  await client.writeMemory(addr, data);
  console.log(`Loaded ${data.length} bytes from ${file} at $${hex16(addr)}`);
}

function cmdSym(args) {
  const file = args[0];
  if (!file) { console.error('Usage: sym <file>'); return; }
  const count = loadSymbols(file);
  console.log(`Loaded ${count} symbols from ${file}`);
  // Print them
  for (const [name, addr] of symbols) {
    console.log(`  $${hex16(addr)}  ${name}`);
  }
}

async function cmdBp(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: bp <addr|label>'); return; }
  await client.setBreakpoint(addr);
  const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';
  console.log(`Breakpoint set at $${hex16(addr)}${label}`);
}

async function cmdBpc(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: bpc <addr|label>'); return; }
  await client.clearBreakpoint(addr);
  console.log(`Breakpoint cleared at $${hex16(addr)}`);
}

async function cmdRun(client, args) {
  let timeout = 30000;
  const tIdx = args.indexOf('--timeout');
  if (tIdx >= 0 && args[tIdx + 1]) timeout = parseInt(args[tIdx + 1], 10);
  console.log('Continuing...');
  const reply = await client.continue(timeout);
  console.log(fmtStop(reply));
  await cmdRegs(client);
}

async function cmdStep(client, args) {
  const n = args[0] ? parseInt(args[0], 10) : 1;
  for (let i = 0; i < n; i++) {
    const reply = await client.step();
    if (i === n - 1 || !reply.startsWith('T05') && !reply.startsWith('S05')) {
      console.log(fmtStop(reply));
      await cmdRegs(client);
      if (!reply.startsWith('T05') && !reply.startsWith('S05')) break;
    }
  }
}

async function cmdHalt(client) {
  const reply = await client.pause();
  console.log(fmtStop(reply));
  await cmdRegs(client);
}

async function cmdReset(client) {
  // Write PC to reset vector location, or use monitor reset if supported
  // The N8machine stub doesn't have a reset command, so read reset vector and set PC
  const vec = await client.readMemory(0xFFFC, 2);
  const resetAddr = vec[0] | (vec[1] << 8);
  await client.writeRegister(4, resetAddr);
  console.log(`Reset: PC set to $${hex16(resetAddr)} (from reset vector)`);
  await cmdRegs(client);
}

const REG_NAMES = { a: 0, x: 1, y: 2, s: 3, sp: 3, pc: 4, p: 5, sr: 5 };

async function cmdWreg(client, args) {
  const name = args[0]?.toLowerCase();
  const val = parseAddr(args[1]);
  if (!name || isNaN(val) || !(name in REG_NAMES)) {
    console.error('Usage: wreg <a|x|y|s|p|pc> <value>');
    return;
  }
  const id = REG_NAMES[name];
  await client.writeRegister(id, val);
  const width = id === 4 ? 4 : 2;
  console.log(`${name.toUpperCase()} = $${val.toString(16).padStart(width, '0')}`);
  await cmdRegs(client);
}

async function cmdPc(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: pc <addr|label>'); return; }
  await client.writeRegister(4, addr);
  const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';
  console.log(`PC set to $${hex16(addr)}${label}`);
  await cmdRegs(client);
}

async function cmdGoto(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: goto <addr|label>'); return; }
  await client.writeRegister(4, addr);
  const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';
  console.log(`PC set to $${hex16(addr)}${label}, continuing...`);
  let timeout = 30000;
  const tIdx = args.indexOf('--timeout');
  if (tIdx >= 0 && args[tIdx + 1]) timeout = parseInt(args[tIdx + 1], 10);
  const reply = await client.continue(timeout);
  console.log(fmtStop(reply));
  await cmdRegs(client);
}

// ── N8 character map (byte → Unicode) ───────────────────────────

// prettier-ignore
const N8_CHARMAP = [
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

// ── Console text ────────────────────────────────────────────────

async function cmdConsoleText(client) {
  // Read video registers at $D840 (9 bytes: mode..vsync)
  const regs = await client.readMemory(0xD840, 9);
  const mode   = regs[0];
  const width  = regs[1];
  const height = regs[2];
  const stride = regs[3];
  const curStyle = regs[5];
  const curCol   = regs[6];
  const curRow   = regs[7];

  const fbSize = stride * height;
  if (fbSize === 0 || fbSize > 0x1000) {
    console.error('Invalid video dimensions');
    return;
  }

  const fb = await client.readMemory(0xC000, fbSize);

  const modeStr = mode === 0 ? 'Text Default' : mode === 1 ? 'Text Custom' : `0x${hex8(mode)}`;
  const curModeStr = (curStyle & 0x03) === 0 ? 'off' : (curStyle & 0x03) === 1 ? 'steady' : 'flash';
  const curShapeStr = (curStyle & 0x0C) === 0x04 ? 'block' : 'underline';
  console.log(`Mode: ${modeStr}  Size: ${width}\u00D7${height}  Stride: ${stride}  Cursor: (${curCol},${curRow}) ${curModeStr} ${curShapeStr}`);
  console.log('\u2500'.repeat(width));

  for (let row = 0; row < height; row++) {
    let line = '';
    for (let col = 0; col < width; col++) {
      const byte = fb[row * stride + col];
      line += N8_CHARMAP[byte] || '?';
    }
    console.log(line);
  }
  console.log('\u2500'.repeat(width));
}

// ── Console video ────────────────────────────────────────────────

async function cmdConsoleVideo(client, args) {
  const path = args[0];
  if (!path) { console.error('Usage: console_video <path>'); return; }
  const hexData = await client.readXfer('n8screen');
  const png = Buffer.from(hexData, 'hex');
  writeFileSync(path, png);
  console.log(`Screenshot saved: ${path} (${png.length} bytes)`);
}

// ── Help ────────────────────────────────────────────────────────

function cmdHelp() {
  console.log([
    'n8gdb — GDB RSP client for N8machine 6502 emulator',
    '',
    'Usage: n8gdb [--host HOST] [--port PORT] [--sym FILE] <command> [args...]',
    '',
    'Commands:',
    '  regs                        Read all CPU registers',
    '  wreg  <reg> <val>           Write register (a x y s p pc)',
    '  pc    <addr|label>          Set PC (safe — sets SYNC state)',
    '  goto  <addr|label>          Set PC and continue execution',
    '  read  <addr> [len]          Read memory (hex dump)',
    '  write <addr> <hex|@file>    Write hex bytes or load file to address',
    '  load  <file> <addr>         Load binary file at address',
    '  sym   <file>                Load cc65 .sym file, print labels',
    '  bp    <addr|label>          Set breakpoint',
    '  bpc   <addr|label>          Clear breakpoint',
    '  run   [--timeout ms]        Continue execution (wait for stop)',
    '  step  [n]                   Single step (n times)',
    '  halt                        Send interrupt (Ctrl-C)',
    '  reset                       Reset CPU',
    '  kbd_inject <input>          Inject keystrokes into keyboard buffer',
    '  console_text                Read video text buffer as Unicode',
    '  console_video <path>        Save screen screenshot as PNG',
    '  detach                      Detach and disconnect',
    '  help                        Show this help',
    '  repl                        Interactive REPL mode',
    '',
    'kbd_inject input formats:',
    '  kbd_inject 0x41             Hex keycode',
    '  kbd_inject \'A\'              ASCII char',
    '  kbd_inject "Hello"          String (injects each char)',
    '  kbd_inject enter            Named key (enter esc tab backspace delete',
    '                              up down left right home end pageup pagedown',
    '                              insert f1-f12 space)',
    '  Modifier flags: --shift --ctrl --alt --caps --mod 0xNN',
    '',
    'Addresses: hex with 0x or $ prefix, or bare hex. Decimal with # prefix.',
    'Labels: if a .sym file is loaded (--sym), label names resolve to addresses.',
    '',
    'Environment:',
    '  N8GDB_HOST    Default host (127.0.0.1)',
    '  N8GDB_PORT    Default port (3333)',
    '  N8GDB_SYM     Default .sym file path',
    '  N8GDB_DEBUG   Set to 1 for RSP debug logging',
  ].join('\n'));
}

// ── Named keys and auto-modifier tables ─────────────────────────

const NAMED_KEYS = {
  enter: 0x0D, return: 0x0D, cr: 0x0D,
  backspace: 0x08, bs: 0x08,
  tab: 0x09,
  esc: 0x1B, escape: 0x1B,
  delete: 0x87, del: 0x87,
  space: 0x20,
  up: 0x80, down: 0x81, left: 0x82, right: 0x83,
  home: 0x84, end: 0x85, pageup: 0x86, pagedown: 0x88, insert: 0x89,
  f1: 0x90, f2: 0x91, f3: 0x92, f4: 0x93, f5: 0x94, f6: 0x95,
  f7: 0x96, f8: 0x97, f9: 0x98, f10: 0x99, f11: 0x9A, f12: 0x9B,
};

function charToKeycode(ch) {
  const code = ch.charCodeAt(0);
  // Printable ASCII $20-$7E: send ASCII code directly as keycode
  // (N8 keycode space includes the full printable ASCII range)
  if (code >= 0x20 && code <= 0x7E) {
    return { keycode: code, modifiers: 0x00 };
  }
  return null;
}

async function cmdKbdInject(client, args) {
  // Parse modifier flags
  let extraMod = 0;
  const inputArgs = [];
  for (let i = 0; i < args.length; i++) {
    switch (args[i]) {
      case '--shift': extraMod |= 0x04; break;
      case '--ctrl':  extraMod |= 0x08; break;
      case '--alt':   extraMod |= 0x10; break;
      case '--caps':  extraMod |= 0x20; break;
      case '--mod':
        if (args[i + 1]) extraMod |= parseInt(args[++i], 16) & 0x3C;
        break;
      default:
        inputArgs.push(args[i]);
    }
  }

  if (inputArgs.length === 0) {
    console.error('Usage: kbd_inject <key|"string"|0xNN|named_key> [--shift] [--ctrl] [--alt] [--caps] [--mod 0xNN]');
    return;
  }

  const input = inputArgs.join(' ');
  const keys = [];  // [{keycode, modifiers}, ...]

  // String mode: "Hello" or 'x'
  if ((input.startsWith('"') && input.endsWith('"')) ||
      (input.startsWith("'") && input.endsWith("'"))) {
    const str = input.slice(1, -1);
    if (str.length === 0) { console.error('Empty string'); return; }
    for (const ch of str) {
      const kc = charToKeycode(ch);
      if (!kc) { console.error(`Cannot map character: ${ch}`); return; }
      keys.push({ keycode: kc.keycode, modifiers: kc.modifiers | extraMod });
    }
  }
  // Named key
  else if (input.toLowerCase() in NAMED_KEYS) {
    keys.push({ keycode: NAMED_KEYS[input.toLowerCase()], modifiers: extraMod });
  }
  // Hex keycode
  else {
    const addr = parseAddr(input);
    if (isNaN(addr) || addr > 0xFF) {
      console.error(`Invalid keycode: ${input}`);
      return;
    }
    keys.push({ keycode: addr, modifiers: extraMod });
  }

  for (const { keycode, modifiers } of keys) {
    const reply = await client.monitorCommand(`kbd ${hex8(keycode)} ${hex8(modifiers)}`);
    if (reply !== 'OK') {
      console.error(`kbd_inject failed: ${reply}`);
      return;
    }
  }
  console.log(`Injected ${keys.length} key${keys.length > 1 ? 's' : ''}`);
}

// ── REPL ────────────────────────────────────────────────────────

async function repl(client) {
  const rl = createInterface({
    input: process.stdin,
    output: process.stderr,
    prompt: 'n8> ',
    terminal: process.stdin.isTTY ?? false,
  });

  console.error('n8gdb REPL — type "help" for commands, "quit" to exit');
  rl.prompt();

  for await (const line of rl) {
    const parts = line.trim().split(/\s+/);
    const cmd = parts[0]?.toLowerCase();
    const args = parts.slice(1);
    if (!cmd) { rl.prompt(); continue; }

    try {
      switch (cmd) {
        case 'regs': case 'r':      await cmdRegs(client); break;
        case 'wreg':                 await cmdWreg(client, args); break;
        case 'pc':                   await cmdPc(client, args); break;
        case 'goto': case 'g':      await cmdGoto(client, args); break;
        case 'read': case 'rd': case 'm':  await cmdRead(client, args); break;
        case 'write': case 'wr':    await cmdWrite(client, args); break;
        case 'load': case 'l':      await cmdLoad(client, args); break;
        case 'sym':                  cmdSym(args); break;
        case 'bp': case 'b':        await cmdBp(client, args); break;
        case 'bpc': case 'bc':      await cmdBpc(client, args); break;
        case 'run': case 'c':       await cmdRun(client, args); break;
        case 'step': case 's':      await cmdStep(client, args); break;
        case 'halt': case 'h':      await cmdHalt(client); break;
        case 'reset':               await cmdReset(client); break;
        case 'kbd_inject': case 'ki': await cmdKbdInject(client, args); break;
        case 'console_text': case 'ct': await cmdConsoleText(client); break;
        case 'console_video': case 'cv': await cmdConsoleVideo(client, args); break;
        case 'detach':              await client.detach(); console.log('Detached'); rl.close(); return;
        case 'quit': case 'q':      client.disconnect(); rl.close(); return;
        case 'help': case '?':
          console.log([
            'Commands:',
            '  regs|r                  Read CPU registers',
            '  wreg <reg> <val>        Write register (a x y s p pc)',
            '  pc <addr|label>         Set PC',
            '  goto|g <addr|label>     Set PC and continue',
            '  read|rd|m <addr> [len]  Read memory (hex dump)',
            '  write|wr <addr> <hex>   Write hex bytes to memory',
            '  load|l <file> <addr>    Load binary file',
            '  sym <file>              Load .sym file',
            '  bp|b <addr|label>       Set breakpoint',
            '  bpc|bc <addr|label>     Clear breakpoint',
            '  run|c [--timeout ms]    Continue execution',
            '  step|s [n]              Single step',
            '  halt|h                  Interrupt execution',
            '  reset                   Reset CPU to reset vector',
            '  kbd_inject|ki <input>   Inject keystrokes',
            '  console_text|ct         Read screen as Unicode text',
            '  console_video|cv <path> Save screen as PNG',
            '  detach                  Detach from target',
            '  quit|q                  Disconnect and exit',
          ].join('\n'));
          break;
        default:
          console.error(`Unknown command: ${cmd}. Type "help" for commands.`);
      }
    } catch (err) {
      console.error(`Error: ${err.message}`);
    }
    rl.prompt();
  }
}

// ── Main ────────────────────────────────────────────────────────

async function main() {
  const argv = process.argv.slice(2);

  // Parse global flags
  let host = process.env.N8GDB_HOST || '127.0.0.1';
  let port = parseInt(process.env.N8GDB_PORT || '3333', 10);
  let symFile = process.env.N8GDB_SYM || null;

  // Extract --host, --port, --sym from argv
  const cmdArgs = [];
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--host' && argv[i + 1]) { host = argv[++i]; }
    else if (argv[i] === '--port' && argv[i + 1]) { port = parseInt(argv[++i], 10); }
    else if (argv[i] === '--sym' && argv[i + 1]) { symFile = argv[++i]; }
    else { cmdArgs.push(argv[i]); }
  }

  // Load symbols if specified
  if (symFile) {
    if (!existsSync(symFile)) { console.error(`Symbol file not found: ${symFile}`); process.exit(1); }
    const count = loadSymbols(symFile);
    if (process.env.N8GDB_DEBUG === '1') console.error(`Loaded ${count} symbols from ${symFile}`);
  }

  const cmd = cmdArgs[0]?.toLowerCase();
  const args = cmdArgs.slice(1);

  if (!cmd) {
    process.stderr.write([
      'Usage: n8gdb [--host H] [--port P] [--sym FILE] <command> [args...]',
      'Commands: regs wreg pc goto read write load sym bp bpc run step halt reset detach repl',
      'Run "n8gdb repl" for interactive mode.',
      '',
    ].join('\n'));
    process.exit(2);
  }

  // Commands that don't need a connection
  if (cmd === 'help') {
    cmdHelp();
    process.exit(0);
  }
  if (cmd === 'sym') {
    cmdSym(args);
    process.exit(0);
  }

  // Connect
  const client = new RspClient();
  try {
    await client.connect(host, port);
  } catch (err) {
    console.error(`Failed to connect to ${host}:${port}: ${err.message}`);
    process.exit(1);
  }

  try {
    switch (cmd) {
      case 'regs':    await cmdRegs(client); break;
      case 'wreg':    await cmdWreg(client, args); break;
      case 'pc':      await cmdPc(client, args); break;
      case 'goto':    await cmdGoto(client, args); break;
      case 'read':    await cmdRead(client, args); break;
      case 'write':   await cmdWrite(client, args); break;
      case 'load':    await cmdLoad(client, args); break;
      case 'bp':      await cmdBp(client, args); break;
      case 'bpc':     await cmdBpc(client, args); break;
      case 'run':     await cmdRun(client, args); break;
      case 'step':    await cmdStep(client, args); break;
      case 'halt':    await cmdHalt(client); break;
      case 'reset':   await cmdReset(client); break;
      case 'kbd_inject': await cmdKbdInject(client, args); break;
      case 'console_text': await cmdConsoleText(client); break;
      case 'console_video': await cmdConsoleVideo(client, args); break;
      case 'detach':  await client.detach(); break;
      case 'repl':    await repl(client); break;
      default:
        console.error(`Unknown command: ${cmd}`);
        process.exit(2);
    }
  } catch (err) {
    console.error(`Error: ${err.message}`);
    process.exit(1);
  }

  client.disconnect();
}

main();
