#!/usr/bin/env node

/**
 * n8mcp — MCP server for N8machine 6502 emulator.
 *
 * Provides tool-based access to the N8machine emulator via GDB RSP protocol.
 * Designed for use with Claude Code and other MCP clients.
 *
 * Environment:
 *   N8_HOME       Path to N8machine repo root (required for n8_start)
 *   N8GDB_HOST    GDB stub host (default: 127.0.0.1)
 *   N8GDB_PORT    GDB stub port (default: 3333)
 *   N8GDB_DEBUG   Set to 1 for RSP debug logging
 */

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { readFileSync, writeFileSync, existsSync } from 'fs';
import { spawn } from 'child_process';
import { createConnection } from 'net';

import { RspClient } from '../n8gdb/rsp.mjs';
import { loadSymbols } from '../shared/symbols.mjs';
import { parseAddr } from '../shared/address.mjs';
import { hexdump, fmtRegs, fmtStop, hex8, hex16 } from '../shared/format.mjs';
import { N8_CHARMAP } from '../shared/charmap.mjs';
import { parseKeyInput } from '../shared/keyboard.mjs';

// ── State ────────────────────────────────────────────────────────

let client = null;
let n8Process = null;
const symbols = new Map();
const addrLabels = new Map();

const HOST = process.env.N8GDB_HOST || '127.0.0.1';
const PORT = parseInt(process.env.N8GDB_PORT || '3333', 10);
const N8_HOME = process.env.N8_HOME || null;

// ── Connection management ────────────────────────────────────────

async function ensureConnected() {
  if (client?.connected) return;
  client = new RspClient();
  await client.connect(HOST, PORT);
}

function resolveAddr(str) {
  return parseAddr(str, symbols);
}

/**
 * Execute a read-only operation that auto-resumes the CPU if it was running.
 */
async function withAutoResume(fn) {
  await ensureConnected();
  const wasRunning = client.wasRunning;
  try {
    return await fn(client);
  } finally {
    if (wasRunning) client.continueAsync();
  }
}

/**
 * Execute an operation that does NOT auto-resume (mutating operations).
 */
async function withConnection(fn) {
  await ensureConnected();
  return await fn(client);
}

// ── Process management ───────────────────────────────────────────

function isPortOpen(host, port, timeoutMs = 1000) {
  return new Promise((resolve) => {
    const sock = createConnection({ host, port }, () => {
      sock.destroy();
      resolve(true);
    });
    sock.on('error', () => resolve(false));
    sock.setTimeout(timeoutMs, () => { sock.destroy(); resolve(false); });
  });
}

async function waitForPort(host, port, maxWaitMs = 10000) {
  const start = Date.now();
  while (Date.now() - start < maxWaitMs) {
    if (await isPortOpen(host, port)) return true;
    await new Promise(r => setTimeout(r, 250));
  }
  return false;
}

async function findN8Pid() {
  try {
    const { execSync } = await import('child_process');
    const out = execSync(`lsof -ti :${PORT} 2>/dev/null`, { encoding: 'utf8' }).trim();
    return out ? parseInt(out.split('\n')[0], 10) : null;
  } catch { return null; }
}

async function startN8() {
  if (await isPortOpen(HOST, PORT)) {
    return { alreadyRunning: true, pid: await findN8Pid() };
  }
  if (!N8_HOME) {
    throw new Error('N8_HOME environment variable not set. Cannot start n8.');
  }
  if (!existsSync(`${N8_HOME}/n8`) && !existsSync(`${N8_HOME}/build/n8`)) {
    throw new Error(`n8 binary not found in ${N8_HOME}. Run 'make' first.`);
  }
  const binary = existsSync(`${N8_HOME}/n8`) ? './n8' : './build/n8';

  // Disconnect existing client if any
  if (client?.connected) { client.disconnect(); client = null; }

  n8Process = spawn(binary, [], {
    cwd: N8_HOME,
    detached: true,
    stdio: 'ignore',
    env: { ...process.env },
  });
  n8Process.unref();
  const pid = n8Process.pid;

  const ready = await waitForPort(HOST, PORT);
  if (!ready) {
    throw new Error(`n8 started (PID ${pid}) but port ${PORT} not ready after 10s`);
  }
  return { alreadyRunning: false, pid };
}

async function stopN8() {
  if (client?.connected) { client.disconnect(); client = null; }

  const pid = n8Process?.pid || await findN8Pid();
  if (!pid) {
    throw new Error('No n8 process found');
  }
  try {
    process.kill(pid, 'SIGTERM');
  } catch (e) {
    if (e.code === 'ESRCH') throw new Error(`Process ${pid} not found`);
    throw e;
  }
  n8Process = null;

  // Wait for port to close
  const start = Date.now();
  while (Date.now() - start < 5000) {
    if (!(await isPortOpen(HOST, PORT))) return pid;
    await new Promise(r => setTimeout(r, 250));
  }
  return pid;
}

// ── Console text helper ──────────────────────────────────────────

async function readConsoleText(rsp) {
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

// ── MCP Server ───────────────────────────────────────────────────

const server = new McpServer({
  name: 'n8machine',
  version: '1.0.0',
});

// -- Process management --

server.tool('n8_start', 'Start the N8machine emulator. No-op if already running.', {},
  async () => {
    try {
      const result = await startN8();
      if (result.alreadyRunning) {
        return { content: [{ type: 'text', text: `n8 already running (PID ${result.pid || 'unknown'})` }] };
      }
      return { content: [{ type: 'text', text: `n8 started (PID ${result.pid}), port ${PORT} ready` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_stop', 'Stop the N8machine emulator.', {},
  async () => {
    try {
      const pid = await stopN8();
      return { content: [{ type: 'text', text: `n8 stopped (PID ${pid})` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_restart', 'Restart the N8machine emulator.', {},
  async () => {
    try {
      try { await stopN8(); } catch { /* may not be running */ }
      await new Promise(r => setTimeout(r, 500));
      const result = await startN8();
      return { content: [{ type: 'text', text: `n8 restarted (PID ${result.pid}), port ${PORT} ready` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// -- CPU state --

server.tool('n8_regs', 'Read all 6502 CPU registers (A, X, Y, S, P, PC) with decoded flags. Auto-resumes if CPU was running.', {},
  async () => {
    try {
      const text = await withAutoResume(async (rsp) => {
        const r = await rsp.readRegisters();
        return fmtRegs(r, addrLabels);
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_write_reg',
  'Write a CPU register by name. Returns updated registers.',
  { register: z.enum(['a', 'x', 'y', 's', 'p', 'pc']).describe('Register name'), value: z.string().describe('Value (hex with 0x/$, decimal with #, or bare hex)') },
  async ({ register, value }) => {
    try {
      const REG_IDS = { a: 0, x: 1, y: 2, s: 3, pc: 4, p: 5 };
      const id = REG_IDS[register];
      const val = resolveAddr(value);
      if (isNaN(val)) return { content: [{ type: 'text', text: `Invalid value: ${value}` }], isError: true };

      const text = await withConnection(async (rsp) => {
        await rsp.writeRegister(id, val);
        const r = await rsp.readRegisters();
        const width = id === 4 ? 4 : 2;
        return `${register.toUpperCase()} = $${val.toString(16).padStart(width, '0')}\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_status',
  'Show CPU state (running/halted + registers) without changing execution state. Auto-resumes if CPU was running.',
  {},
  async () => {
    try {
      const text = await withAutoResume(async (rsp) => {
        const state = rsp.wasRunning ? 'Running' : 'Halted';
        const r = await rsp.readRegisters();
        return `CPU: ${state}\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// -- Memory --

server.tool('n8_read_memory',
  'Read memory. Returns hex dump with ASCII. Accepts labels if symbols loaded. Auto-resumes.',
  { address: z.string().describe('Address (hex, label, or decimal with #)'), length: z.number().default(16).describe('Number of bytes to read (default 16)') },
  async ({ address, length }) => {
    try {
      const addr = resolveAddr(address);
      if (isNaN(addr)) return { content: [{ type: 'text', text: `Invalid address: ${address}` }], isError: true };

      const text = await withAutoResume(async (rsp) => {
        const buf = await rsp.readMemory(addr, length);
        return hexdump(buf, addr);
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_write_memory',
  'Write hex bytes to a memory address.',
  { address: z.string().describe('Address (hex, label, or decimal with #)'), hex_data: z.string().describe('Hex string of bytes to write (e.g. "a9428d00")') },
  async ({ address, hex_data }) => {
    try {
      const addr = resolveAddr(address);
      if (isNaN(addr)) return { content: [{ type: 'text', text: `Invalid address: ${address}` }], isError: true };
      const clean = hex_data.replace(/[\s,]/g, '');
      if (clean.length === 0) return { content: [{ type: 'text', text: 'Empty hex string' }], isError: true };
      if (clean.length % 2 !== 0) return { content: [{ type: 'text', text: 'Hex string must have even number of digits' }], isError: true };
      if (!/^[0-9a-fA-F]+$/.test(clean)) return { content: [{ type: 'text', text: 'Invalid hex characters' }], isError: true };

      const data = Buffer.from(clean, 'hex');
      const text = await withConnection(async (rsp) => {
        await rsp.writeMemory(addr, data);
        return `Wrote ${data.length} bytes at $${hex16(addr)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_load_binary',
  'Load a binary file into memory at the given address. Optionally load a .sym file for labels.',
  { file: z.string().describe('Path to binary file'), address: z.string().describe('Load address (hex, decimal with #)'), sym_file: z.string().optional().describe('Path to .sym file for label resolution') },
  async ({ file, address, sym_file }) => {
    try {
      const addr = resolveAddr(address);
      if (isNaN(addr)) return { content: [{ type: 'text', text: `Invalid address: ${address}` }], isError: true };
      if (!existsSync(file)) return { content: [{ type: 'text', text: `File not found: ${file}` }], isError: true };

      const data = readFileSync(file);
      const lines = [];

      if (sym_file) {
        if (!existsSync(sym_file)) return { content: [{ type: 'text', text: `Symbol file not found: ${sym_file}` }], isError: true };
        const count = loadSymbols(sym_file, symbols, addrLabels);
        lines.push(`Loaded ${count} symbols from ${sym_file}`);
      }

      await withConnection(async (rsp) => {
        await rsp.writeMemory(addr, data);
      });
      lines.push(`Loaded ${data.length} bytes from ${file} at $${hex16(addr)}`);
      return { content: [{ type: 'text', text: lines.join('\n') }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// -- Symbols --

server.tool('n8_load_symbols',
  'Load a cc65 .sym file for label-based address resolution. Persists for the session.',
  { file: z.string().describe('Path to .sym file') },
  async ({ file }) => {
    try {
      if (!existsSync(file)) return { content: [{ type: 'text', text: `File not found: ${file}` }], isError: true };
      const count = loadSymbols(file, symbols, addrLabels);
      const listing = Array.from(symbols.entries()).map(([name, addr]) => `  $${hex16(addr)}  ${name}`).join('\n');
      return { content: [{ type: 'text', text: `Loaded ${count} symbols:\n${listing}` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// -- Execution control --

server.tool('n8_run',
  'Continue execution and wait for a breakpoint, watchpoint, or timeout.',
  { timeout_ms: z.number().default(5000).describe('Timeout in milliseconds (default 5000)') },
  async ({ timeout_ms }) => {
    try {
      const text = await withConnection(async (rsp) => {
        const reply = await rsp.continue(timeout_ms);
        const r = await rsp.readRegisters();
        return `${fmtStop(reply)}\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_step',
  'Single-step the CPU. Returns registers after stepping.',
  { count: z.number().default(1).describe('Number of instructions to step (default 1)') },
  async ({ count }) => {
    try {
      const text = await withConnection(async (rsp) => {
        let lastReply;
        for (let i = 0; i < count; i++) {
          lastReply = await rsp.step();
          if (!lastReply.startsWith('T05') && !lastReply.startsWith('S05')) break;
        }
        const r = await rsp.readRegisters();
        return `${fmtStop(lastReply)} (${count} step${count > 1 ? 's' : ''})\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_halt',
  'Interrupt a running program. Returns registers.',
  {},
  async () => {
    try {
      const text = await withConnection(async (rsp) => {
        const reply = await rsp.pause();
        const r = await rsp.readRegisters();
        return `${fmtStop(reply)}\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_reset',
  'Reset the CPU by reading the reset vector and setting PC. Returns registers.',
  {},
  async () => {
    try {
      const text = await withConnection(async (rsp) => {
        const vec = await rsp.readMemory(0xFFFC, 2);
        const resetAddr = vec[0] | (vec[1] << 8);
        await rsp.writeRegister(4, resetAddr);
        const r = await rsp.readRegisters();
        return `Reset: PC set to $${hex16(resetAddr)} (from reset vector)\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_goto',
  'Set PC to an address and continue execution. Waits for stop.',
  { address: z.string().describe('Address or label'), timeout_ms: z.number().default(5000).describe('Timeout in milliseconds (default 5000)') },
  async ({ address, timeout_ms }) => {
    try {
      const addr = resolveAddr(address);
      if (isNaN(addr)) return { content: [{ type: 'text', text: `Invalid address: ${address}` }], isError: true };
      const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';

      const text = await withConnection(async (rsp) => {
        await rsp.writeRegister(4, addr);
        const reply = await rsp.continue(timeout_ms);
        const r = await rsp.readRegisters();
        return `Goto $${hex16(addr)}${label}\n${fmtStop(reply)}\n${fmtRegs(r, addrLabels)}`;
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// -- Breakpoints --

server.tool('n8_set_breakpoint',
  'Set an execution breakpoint at an address or label.',
  { address: z.string().describe('Address or label') },
  async ({ address }) => {
    try {
      const addr = resolveAddr(address);
      if (isNaN(addr)) return { content: [{ type: 'text', text: `Invalid address: ${address}` }], isError: true };
      const label = addrLabels.has(addr) ? ` (${addrLabels.get(addr).join(', ')})` : '';

      await withAutoResume(async (rsp) => {
        await rsp.setBreakpoint(addr);
      });
      return { content: [{ type: 'text', text: `Breakpoint set at $${hex16(addr)}${label}` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_clear_breakpoint',
  'Clear a breakpoint at an address or label.',
  { address: z.string().describe('Address or label') },
  async ({ address }) => {
    try {
      const addr = resolveAddr(address);
      if (isNaN(addr)) return { content: [{ type: 'text', text: `Invalid address: ${address}` }], isError: true };

      await withAutoResume(async (rsp) => {
        await rsp.clearBreakpoint(addr);
      });
      return { content: [{ type: 'text', text: `Breakpoint cleared at $${hex16(addr)}` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_clear_all_breakpoints',
  'Clear all breakpoints and watchpoints.',
  {},
  async () => {
    try {
      await withAutoResume(async (rsp) => {
        await rsp.monitorCommand('clear-bp');
      });
      return { content: [{ type: 'text', text: 'Cleared all breakpoints and watchpoints' }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// -- I/O --

server.tool('n8_kbd_inject',
  'Inject keystrokes into the N8 keyboard buffer. Syntax: bare text with [named_key] sequences. Example: "go north[enter]". Named keys: enter, esc, tab, backspace, delete, space, up, down, left, right, home, end, pageup, pagedown, insert, f1-f12. Hex keycodes: [0xNN]. Max 32 keys.',
  { text: z.string().describe('Key string with inline [named_key] sequences'), shift: z.boolean().default(false), ctrl: z.boolean().default(false), alt: z.boolean().default(false) },
  async ({ text, shift, ctrl, alt }) => {
    try {
      let extraMod = 0;
      if (shift) extraMod |= 0x04;
      if (ctrl) extraMod |= 0x08;
      if (alt) extraMod |= 0x10;

      const { keys, error } = parseKeyInput(text, extraMod, resolveAddr);
      if (error) return { content: [{ type: 'text', text: `Error: ${error}` }], isError: true };
      if (keys.length === 0) return { content: [{ type: 'text', text: 'No keys to inject' }], isError: true };
      if (keys.length > 32) return { content: [{ type: 'text', text: `Too many keys (${keys.length}): limit is 32` }], isError: true };

      await withAutoResume(async (rsp) => {
        for (const { keycode, modifiers } of keys) {
          const reply = await rsp.monitorCommand(`kbd ${hex8(keycode)} ${hex8(modifiers)}`);
          if (reply !== 'OK') throw new Error(`kbd_inject failed: ${reply}`);
        }
      });
      return { content: [{ type: 'text', text: `Injected ${keys.length} key${keys.length > 1 ? 's' : ''}` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_console_text',
  'Read the video framebuffer as Unicode text using the N8 character map. Shows video mode, dimensions, cursor position, and full screen content. Auto-resumes.',
  {},
  async () => {
    try {
      const text = await withAutoResume(async (rsp) => {
        return await readConsoleText(rsp);
      });
      return { content: [{ type: 'text', text }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool('n8_console_video',
  'Capture the emulator screen as a PNG screenshot. Saves to the specified path and returns it.',
  { path: z.string().describe('File path to save the PNG screenshot') },
  async ({ path }) => {
    try {
      const result = await withAutoResume(async (rsp) => {
        const hexData = await rsp.readXfer('n8screen');
        const png = Buffer.from(hexData, 'hex');
        writeFileSync(path, png);
        return { size: png.length, path };
      });
      return { content: [{ type: 'text', text: `Screenshot saved: ${result.path} (${result.size} bytes)` }] };
    } catch (e) {
      return { content: [{ type: 'text', text: `Error: ${e.message}` }], isError: true };
    }
  }
);

// ── Start server ─────────────────────────────────────────────────

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((err) => {
  console.error(`n8mcp fatal: ${err.message}`);
  process.exit(1);
});

// Export for testing
export { server, ensureConnected, resolveAddr, withAutoResume, withConnection,
         readConsoleText, startN8, stopN8, isPortOpen, symbols, addrLabels };
