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
import { readFileSync, existsSync } from 'fs';
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
  const len = args[1] ? parseAddr(args[1]) : 16;
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
  await client.writeRegister(5, resetAddr);
  console.log(`Reset: PC set to $${hex16(resetAddr)} (from reset vector)`);
  await cmdRegs(client);
}

const REG_NAMES = { a: 0, x: 1, y: 2, s: 3, sp: 3, p: 4, sr: 4, pc: 5 };

async function cmdWreg(client, args) {
  const name = args[0]?.toLowerCase();
  const val = parseAddr(args[1]);
  if (!name || isNaN(val) || !(name in REG_NAMES)) {
    console.error('Usage: wreg <a|x|y|s|p|pc> <value>');
    return;
  }
  const id = REG_NAMES[name];
  await client.writeRegister(id, val);
  const width = id === 5 ? 4 : 2;
  console.log(`${name.toUpperCase()} = $${val.toString(16).padStart(width, '0')}`);
  await cmdRegs(client);
}

async function cmdPc(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: pc <addr|label>'); return; }
  await client.writeRegister(5, addr);
  const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';
  console.log(`PC set to $${hex16(addr)}${label}`);
  await cmdRegs(client);
}

async function cmdGoto(client, args) {
  const addr = parseAddr(args[0]);
  if (isNaN(addr)) { console.error('Usage: goto <addr|label>'); return; }
  await client.writeRegister(5, addr);
  const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';
  console.log(`PC set to $${hex16(addr)}${label}, continuing...`);
  let timeout = 30000;
  const tIdx = args.indexOf('--timeout');
  if (tIdx >= 0 && args[tIdx + 1]) timeout = parseInt(args[tIdx + 1], 10);
  const reply = await client.continue(timeout);
  console.log(fmtStop(reply));
  await cmdRegs(client);
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

  // sym command doesn't need connection
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
