#!/usr/bin/env node

/**
 * Unit tests for n8mcp MCP server.
 * Tests shared modules, tool handler logic, and mock RSP interactions.
 */

import { createServer } from 'net';
import { readFileSync, writeFileSync, unlinkSync, existsSync, mkdirSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

// Shared modules under test
import { parseAddr } from '../shared/address.mjs';
import { loadSymbols } from '../shared/symbols.mjs';
import { hexdump, fmtRegs, fmtStop, hex8, hex16 } from '../shared/format.mjs';
import { N8_CHARMAP } from '../shared/charmap.mjs';
import { parseKeyInput, NAMED_KEYS, charToKeycode } from '../shared/keyboard.mjs';

// RSP client for mock server tests
import { RspClient } from '../n8gdb/rsp.mjs';

let passed = 0;
let failed = 0;
let currentTest = '';

function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error(`  FAIL [${currentTest}]: ${msg}`); }
}

function assertEq(a, b, msg) {
  if (a === b) { passed++; }
  else { failed++; console.error(`  FAIL [${currentTest}]: ${msg}: expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`); }
}

function assertMatch(str, pattern, msg) {
  if (pattern.test(str)) { passed++; }
  else { failed++; console.error(`  FAIL [${currentTest}]: ${msg}: ${JSON.stringify(str)} does not match ${pattern}`); }
}

// ── Mock GDB Stub Server ────────────────────────────────────────

function checksum(data) {
  let sum = 0;
  for (let i = 0; i < data.length; i++) sum += data.charCodeAt(i);
  return (sum & 0xFF).toString(16).padStart(2, '0');
}

function makePacket(data) {
  return `$${data}#${checksum(data)}`;
}

function createMockServer(responses) {
  const received = [];
  const server = createServer((socket) => {
    let buf = '';
    socket.on('data', (data) => {
      buf += data.toString('binary');
      while (buf.length > 0) {
        if (buf.charCodeAt(0) === 0x03) {
          buf = buf.slice(1);
          received.push('<interrupt>');
          socket.write(makePacket('T02thread:01;'), 'binary');
          continue;
        }
        if (buf[0] === '+' || buf[0] === '-') { buf = buf.slice(1); continue; }
        const dollar = buf.indexOf('$');
        if (dollar === -1) { buf = ''; break; }
        if (dollar > 0) buf = buf.slice(dollar);
        const hash = buf.indexOf('#');
        if (hash === -1 || hash + 2 >= buf.length) break;
        const payload = buf.slice(1, hash);
        buf = buf.slice(hash + 3);
        received.push(payload);
        socket.write('+', 'binary');
        let reply = '';
        for (const [prefix, resp] of responses) {
          if (payload === prefix || payload.startsWith(prefix)) {
            reply = resp;
            break;
          }
        }
        socket.write(makePacket(reply), 'binary');
      }
    });
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => {
      resolve({ port: server.address().port, close: () => server.close(), received });
    });
  });
}

const HANDSHAKE = new Map([
  ['qSupported', 'PacketSize=4000;swbreak+;hwbreak+'],
  ['QStartNoAckMode', 'OK'],
  ['?', 'S05'],
]);

function mockWithHandshake(extra) {
  const map = new Map(HANDSHAKE);
  for (const [k, v] of extra) map.set(k, v);
  return createMockServer(map);
}

// ── Address Parsing Tests ───────────────────────────────────────

function testParseAddr() {
  currentTest = 'parseAddr';

  // Hex with prefix
  assertEq(parseAddr('0x1234'), 0x1234, '0x prefix');
  assertEq(parseAddr('0XFF'), 0xFF, '0X prefix');
  assertEq(parseAddr('$D000'), 0xD000, '$ prefix');

  // Decimal
  assertEq(parseAddr('#255'), 255, '# decimal');
  assertEq(parseAddr('#0'), 0, '# zero');

  // Bare hex
  assertEq(parseAddr('FF'), 0xFF, 'bare hex FF');
  assertEq(parseAddr('d000'), 0xD000, 'bare hex d000');
  assertEq(parseAddr('0400'), 0x0400, 'bare hex 0400');

  // Labels (with symbol map)
  const syms = new Map([['start', 0xE000], ['loop', 0xE010]]);
  assertEq(parseAddr('start', syms), 0xE000, 'label lookup');
  assertEq(parseAddr('loop', syms), 0xE010, 'label lookup');
  assertEq(parseAddr('$D000', syms), 0xD000, 'hex takes priority over label miss');

  // Non-hex chars → NaN (label miss)
  assert(isNaN(parseAddr('foobar')), 'non-hex string returns NaN');
  assert(isNaN(parseAddr('my_label')), 'underscore returns NaN');

  // Edge cases
  assert(isNaN(parseAddr(null)), 'null returns NaN');
  assert(isNaN(parseAddr(undefined)), 'undefined returns NaN');
  assert(isNaN(parseAddr('')), 'empty returns NaN');
}

// ── Symbol Loading Tests ────────────────────────────────────────

function testLoadSymbols() {
  currentTest = 'loadSymbols';

  const tmpFile = join(tmpdir(), `n8mcp_test_sym_${Date.now()}.sym`);
  writeFileSync(tmpFile, [
    'al 00D000 .start',
    'al 00D010 .main_loop',
    'al 00D100 .tty_write',
    'al 000200 .buffer',
    '// comment line',
    '',
    'al 00D000 .entry',  // duplicate address
  ].join('\n'));

  const syms = new Map();
  const labels = new Map();
  const count = loadSymbols(tmpFile, syms, labels);

  assertEq(count, 5, 'symbol count (5 al lines)');
  assertEq(syms.get('start'), 0xD000, 'start symbol');
  assertEq(syms.get('main_loop'), 0xD010, 'main_loop');
  assertEq(syms.get('tty_write'), 0xD100, 'tty_write');
  assertEq(syms.get('buffer'), 0x0200, 'buffer');
  assertEq(syms.get('entry'), 0xD000, 'entry (dup addr)');

  // addrLabels: D000 should have both 'start' and 'entry'
  const d000Labels = labels.get(0xD000);
  assert(d000Labels && d000Labels.includes('start'), 'D000 has start');
  assert(d000Labels && d000Labels.includes('entry'), 'D000 has entry');

  unlinkSync(tmpFile);
}

// ── Formatting Tests ────────────────────────────────────────────

function testHex8() {
  currentTest = 'hex8';
  assertEq(hex8(0), '00', 'zero');
  assertEq(hex8(0xFF), 'ff', '0xFF');
  assertEq(hex8(0x42), '42', '0x42');
}

function testHex16() {
  currentTest = 'hex16';
  assertEq(hex16(0), '0000', 'zero');
  assertEq(hex16(0xD000), 'd000', '0xD000');
  assertEq(hex16(0xFFFF), 'ffff', '0xFFFF');
}

function testHexdump() {
  currentTest = 'hexdump';
  const buf = Buffer.from([0x48, 0x65, 0x6c, 0x6c, 0x6f]);
  const result = hexdump(buf, 0x0200);
  assert(result.includes('0200:'), 'has address');
  assert(result.includes('48 65 6c 6c 6f'), 'has hex bytes');
  assert(result.includes('Hello'), 'has ASCII');

  // Multi-line (> 16 bytes)
  const buf32 = Buffer.alloc(32, 0x41);
  const result32 = hexdump(buf32, 0x1000);
  assert(result32.includes('1000:'), 'first line addr');
  assert(result32.includes('1010:'), 'second line addr');
}

function testFmtRegs() {
  currentTest = 'fmtRegs';
  const r = { a: 0x42, x: 0x0A, y: 0x14, s: 0xFF, p: 0x30, pc: 0xD000 };
  const result = fmtRegs(r);
  assert(result.includes('A:42'), 'A register');
  assert(result.includes('X:0a'), 'X register');
  assert(result.includes('PC:d000'), 'PC register');
  assert(result.includes('Flags:'), 'has flags');

  // With labels
  const labels = new Map([[0xD000, ['start', 'entry']]]);
  const result2 = fmtRegs(r, labels);
  assert(result2.includes('@ start, entry'), 'label annotation');
}

function testFmtStop() {
  currentTest = 'fmtStop';
  assertEq(fmtStop('T05thread:01;'), 'breakpoint hit', 'breakpoint');
  assertEq(fmtStop('T05watch:1234;'), 'watchpoint (write) hit', 'write watchpoint');
  assertEq(fmtStop('T05rwatch:1234;'), 'watchpoint (read) hit', 'read watchpoint');
  assertEq(fmtStop('T05awatch:1234;'), 'watchpoint (access) hit', 'access watchpoint');
  assertEq(fmtStop('T02thread:01;'), 'stopped signal 2', 'signal 2');
  assertEq(fmtStop(null), 'no reply', 'null');
}

// ── Charmap Tests ───────────────────────────────────────────────

function testCharmap() {
  currentTest = 'charmap';
  assertEq(N8_CHARMAP.length, 256, '256 entries');
  assertEq(N8_CHARMAP[0x00], ' ', 'null → space');
  assertEq(N8_CHARMAP[0x20], ' ', 'space');
  assertEq(N8_CHARMAP[0x41], 'A', 'A');
  assertEq(N8_CHARMAP[0x61], 'a', 'a');
  assertEq(N8_CHARMAP[0x7E], '~', 'tilde');
  assertEq(N8_CHARMAP[0x80], '\u2588', 'full block');
}

// ── Keyboard Tests ──────────────────────────────────────────────

function testCharToKeycode() {
  currentTest = 'charToKeycode';
  const a = charToKeycode('A');
  assertEq(a.keycode, 0x41, 'A keycode');
  assertEq(a.modifiers, 0x00, 'A no modifiers');

  const space = charToKeycode(' ');
  assertEq(space.keycode, 0x20, 'space keycode');

  assertEq(charToKeycode('\x01'), null, 'control char returns null');
}

function testParseKeyInput() {
  currentTest = 'parseKeyInput';

  // Simple text
  let r = parseKeyInput('hi');
  assertEq(r.error, null, 'no error');
  assertEq(r.keys.length, 2, '2 keys');
  assertEq(r.keys[0].keycode, 0x68, 'h');
  assertEq(r.keys[1].keycode, 0x69, 'i');

  // Named key
  r = parseKeyInput('[enter]');
  assertEq(r.error, null, 'no error');
  assertEq(r.keys.length, 1, '1 key');
  assertEq(r.keys[0].keycode, 0x0D, 'enter');

  // Mixed text and keys
  r = parseKeyInput('go[enter]');
  assertEq(r.keys.length, 3, '3 keys');
  assertEq(r.keys[2].keycode, 0x0D, 'trailing enter');

  // Hex key
  r = parseKeyInput('[0x41]');
  assertEq(r.keys.length, 1, '1 key');
  assertEq(r.keys[0].keycode, 0x41, 'hex 0x41');

  // Backslash-n
  r = parseKeyInput('a\\nb');
  assertEq(r.keys.length, 3, '3 keys');
  assertEq(r.keys[1].keycode, 0x0D, 'backslash-n → enter');

  // Modifier flags
  r = parseKeyInput('a', 0x04);
  assertEq(r.keys[0].modifiers, 0x04, 'shift modifier');

  // Unknown key
  r = parseKeyInput('[bogus]');
  assert(r.error !== null, 'unknown key error');

  // Empty
  r = parseKeyInput('');
  assertEq(r.keys.length, 0, 'empty input');
}

function testNamedKeys() {
  currentTest = 'namedKeys';
  assertEq(NAMED_KEYS.enter, 0x0D, 'enter');
  assertEq(NAMED_KEYS.esc, 0x1B, 'esc');
  assertEq(NAMED_KEYS.tab, 0x09, 'tab');
  assertEq(NAMED_KEYS.backspace, 0x08, 'backspace');
  assertEq(NAMED_KEYS.up, 0x01, 'up');
  assertEq(NAMED_KEYS.f1, 0x80, 'f1');
  assertEq(NAMED_KEYS.f12, 0x8B, 'f12');
}

// ── Mock RSP Integration Tests ──────────────────────────────────
// These test the tool handler logic via mock RSP servers

async function testMcpRegs() {
  currentTest = 'mcp:n8_regs';
  // A=42, X=0a, Y=14, S=ff, PC=d000(LE: 00d0), P=30
  const mock = await mockWithHandshake(new Map([
    ['g', '420a14ff00d030'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  const r = await client.readRegisters();
  const text = fmtRegs(r);

  assert(text.includes('A:42'), 'regs output has A');
  assert(text.includes('PC:d000'), 'regs output has PC');

  client.disconnect();
  mock.close();
}

async function testMcpReadMemory() {
  currentTest = 'mcp:n8_read_memory';
  const mock = await mockWithHandshake(new Map([
    ['m', '48656c6c6f576f726c6421'],  // HelloWorld!
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  const buf = await client.readMemory(0x0200, 11);
  const text = hexdump(buf, 0x0200);

  assert(text.includes('0200:'), 'hexdump has address');
  assert(text.includes('HelloWorld!'), 'hexdump has ASCII');
  assert(mock.received.includes('m200,b'), 'sent correct m command');

  client.disconnect();
  mock.close();
}

async function testMcpWriteMemory() {
  currentTest = 'mcp:n8_write_memory';
  const mock = await mockWithHandshake(new Map([
    ['M', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const data = Buffer.from('a9428d0002', 'hex');
  await client.writeMemory(0x0400, data);

  assert(mock.received.some(r => r.startsWith('M400,5:a9428d0002')), 'write command sent');

  client.disconnect();
  mock.close();
}

async function testMcpWriteReg() {
  currentTest = 'mcp:n8_write_reg';
  const mock = await mockWithHandshake(new Map([
    ['P', 'OK'],
    ['g', '420a14ff00d030'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // Write A register
  await client.writeRegister(0, 0x42);
  assert(mock.received.some(r => r === 'P0=42'), 'wrote A');

  // Write PC (16-bit LE)
  await client.writeRegister(4, 0xD000);
  assert(mock.received.some(r => r === 'P4=00d0'), 'wrote PC LE');

  client.disconnect();
  mock.close();
}

async function testMcpStep() {
  currentTest = 'mcp:n8_step';
  const mock = await mockWithHandshake(new Map([
    ['s', 'T05thread:01;'],
    ['g', '420a14ff00d030'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const reply = await client.step();
  assert(reply.startsWith('T05'), 'step returns SIGTRAP');

  const r = await client.readRegisters();
  const text = `${fmtStop(reply)}\n${fmtRegs(r)}`;
  assert(text.includes('breakpoint hit'), 'formatted stop');
  assert(text.includes('PC:d000'), 'formatted regs');

  client.disconnect();
  mock.close();
}

async function testMcpRun() {
  currentTest = 'mcp:n8_run';
  const mock = await mockWithHandshake(new Map([
    ['c', 'T05thread:01;'],
    ['g', '420a14ff10e030'],  // PC=e010
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const reply = await client.continue(5000);
  assert(reply.startsWith('T05'), 'run returns stop reply');

  const r = await client.readRegisters();
  assertEq(r.pc, 0xE010, 'PC after run');

  client.disconnect();
  mock.close();
}

async function testMcpHalt() {
  currentTest = 'mcp:n8_halt';
  // Server that only responds to interrupt, not 'c'
  const received = [];
  const server = createServer((socket) => {
    let buf = '';
    socket.on('data', (data) => {
      buf += data.toString('binary');
      while (buf.length > 0) {
        if (buf.charCodeAt(0) === 0x03) {
          buf = buf.slice(1);
          received.push('<interrupt>');
          socket.write(makePacket('T02thread:01;'), 'binary');
          continue;
        }
        if (buf[0] === '+' || buf[0] === '-') { buf = buf.slice(1); continue; }
        const dollar = buf.indexOf('$');
        if (dollar === -1) { buf = ''; break; }
        if (dollar > 0) buf = buf.slice(dollar);
        const hash = buf.indexOf('#');
        if (hash === -1 || hash + 2 >= buf.length) break;
        const payload = buf.slice(1, hash);
        buf = buf.slice(hash + 3);
        received.push(payload);
        socket.write('+', 'binary');
        if (payload.startsWith('qSupported')) socket.write(makePacket('PacketSize=4000'), 'binary');
        else if (payload === 'QStartNoAckMode') socket.write(makePacket('OK'), 'binary');
        else if (payload === '?') socket.write(makePacket('S05'), 'binary');
        else if (payload === 'g') socket.write(makePacket('420a14ff00d030'), 'binary');
        // 'c' command: don't respond
      }
    });
  });

  const { port } = await new Promise(resolve => {
    server.listen(0, '127.0.0.1', () => resolve({ port: server.address().port }));
  });

  const client = new RspClient();
  await client.connect('127.0.0.1', port);
  client.continueAsync();
  await new Promise(r => setTimeout(r, 100));

  const reply = await client.pause();
  assert(reply.startsWith('T02') || reply.startsWith('S'), 'halt returns stop reply');
  assert(received.includes('<interrupt>'), 'sent interrupt byte');

  const r = await client.readRegisters();
  assert(r.pc !== undefined, 'can read regs after halt');

  client.disconnect();
  server.close();
}

async function testMcpReset() {
  currentTest = 'mcp:n8_reset';
  const mock = await mockWithHandshake(new Map([
    ['mfffc,2', '00e0'],  // reset vector → $E000 (LE)
    ['P', 'OK'],
    ['g', '000000ff00e030'],  // PC=e000 after reset
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // Read reset vector
  const vec = await client.readMemory(0xFFFC, 2);
  const resetAddr = vec[0] | (vec[1] << 8);
  assertEq(resetAddr, 0xE000, 'reset vector');

  // Set PC
  await client.writeRegister(4, resetAddr);
  assert(mock.received.some(r => r === 'P4=00e0'), 'set PC to reset vector');

  client.disconnect();
  mock.close();
}

async function testMcpBreakpoints() {
  currentTest = 'mcp:n8_breakpoints';
  const mock = await mockWithHandshake(new Map([
    ['Z0', 'OK'],
    ['z0', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  await client.setBreakpoint(0xD000);
  assert(mock.received.some(r => r === 'Z0,d000,1'), 'set BP');

  await client.clearBreakpoint(0xD000);
  assert(mock.received.some(r => r === 'z0,d000,1'), 'clear BP');

  client.disconnect();
  mock.close();
}

async function testMcpClearAllBp() {
  currentTest = 'mcp:n8_clear_all_bp';
  const mock = await mockWithHandshake(new Map([
    ['qRcmd', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const reply = await client.monitorCommand('clear-bp');
  assertEq(reply, 'OK', 'clear-bp OK');

  // Verify the hex-encoded command was sent
  const clearBpHex = Buffer.from('clear-bp', 'utf8').toString('hex');
  assert(mock.received.some(r => r === `qRcmd,${clearBpHex}`), 'sent qRcmd clear-bp');

  client.disconnect();
  mock.close();
}

async function testMcpKbdInject() {
  currentTest = 'mcp:n8_kbd_inject';
  const mock = await mockWithHandshake(new Map([
    ['qRcmd', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // Inject 'A' (keycode 0x41, modifiers 0x00)
  const { keys } = parseKeyInput('A');
  assertEq(keys.length, 1, '1 key');
  assertEq(keys[0].keycode, 0x41, 'A keycode');

  const reply = await client.monitorCommand(`kbd ${hex8(keys[0].keycode)} ${hex8(keys[0].modifiers)}`);
  assertEq(reply, 'OK', 'kbd inject OK');

  client.disconnect();
  mock.close();
}

async function testMcpConsoleText() {
  currentTest = 'mcp:n8_console_text';

  // Video regs (12 bytes): mode=0, width=40(0x28), height=2, stride=40(0x28), oper=0, cursor_style=1, col=5, row=1, vsync=0, ctrl=0, data=0, status=0
  const videoRegsHex = '002802280001050100000000';
  // Framebuffer: 2 rows of 40 chars, "Hello" + spaces, then empty row
  const row1 = '48656c6c6f' + '00'.repeat(35);
  const row2 = '00'.repeat(40);

  // The mock matches command prefixes, so 'md840' matches readMemory(0xD840,...)
  // and 'mc000' matches readMemory(0xC000,...)
  const mock = await mockWithHandshake(new Map([
    ['md840', videoRegsHex],
    ['mc000', row1 + row2],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const regs = await client.readMemory(0xD840, 12);
  const width = regs[1];
  const height = regs[2];
  const stride = regs[3];

  assertEq(width, 0x28, 'video width');
  assertEq(height, 2, 'video height');
  assertEq(stride, 0x28, 'video stride');

  client.disconnect();
  mock.close();
}

async function testMcpConsoleVideo() {
  currentTest = 'mcp:n8_console_video';
  // PNG magic bytes in hex
  const pngHex = '89504e470d0a1a0a' + '00'.repeat(16);

  const mock = await mockWithHandshake(new Map([
    ['qXfer:n8screen:read', `l${pngHex}`],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const hexData = await client.readXfer('n8screen');
  const png = Buffer.from(hexData, 'hex');

  // Verify PNG magic
  assertEq(png[0], 0x89, 'PNG byte 0');
  assertEq(png[1], 0x50, 'PNG byte 1 (P)');
  assertEq(png[2], 0x4E, 'PNG byte 2 (N)');
  assertEq(png[3], 0x47, 'PNG byte 3 (G)');

  // Save and verify
  const tmpPath = join(tmpdir(), `n8mcp_test_screenshot_${Date.now()}.png`);
  writeFileSync(tmpPath, png);
  assert(existsSync(tmpPath), 'screenshot file created');
  unlinkSync(tmpPath);

  client.disconnect();
  mock.close();
}

async function testMcpGoto() {
  currentTest = 'mcp:n8_goto';
  const mock = await mockWithHandshake(new Map([
    ['P', 'OK'],
    ['c', 'T05thread:01;'],
    ['g', '000000ff10e030'],  // PC=e010
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // Set PC to E000
  await client.writeRegister(4, 0xE000);
  assert(mock.received.some(r => r === 'P4=00e0'), 'set PC for goto');

  // Continue
  const reply = await client.continue(5000);
  assert(reply.startsWith('T05'), 'goto stop reply');

  const r = await client.readRegisters();
  assertEq(r.pc, 0xE010, 'PC after goto');

  client.disconnect();
  mock.close();
}

async function testAutoResume() {
  currentTest = 'mcp:auto_resume';
  // Connect to a server that reports T00 (was running)
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000;swbreak+;hwbreak+'],
    ['QStartNoAckMode', 'OK'],
    ['?', 'T00thread:01;'],  // T00 = was running
    ['g', '420a14ff00d030'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  assert(client.wasRunning, 'wasRunning should be true for T00');

  // Read regs (should work while halted for inspection)
  const r = await client.readRegisters();
  assertEq(r.a, 0x42, 'can read regs');

  // In the real MCP, withAutoResume would call continueAsync() here
  // We just verify the flag
  assert(client.wasRunning, 'wasRunning still true');

  client.disconnect();
  mock.close();
}

async function testAutoResumeNotRunning() {
  currentTest = 'mcp:auto_resume_not_running';
  const mock = await mockWithHandshake(new Map([
    ['g', '420a14ff00d030'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // S05 = halted, wasRunning should be false
  assert(!client.wasRunning, 'wasRunning should be false for S05');

  client.disconnect();
  mock.close();
}

async function testSymbolAddressResolution() {
  currentTest = 'mcp:symbol_address_resolution';

  // Load symbols and verify they work with parseAddr
  const tmpFile = join(tmpdir(), `n8mcp_test_resolve_${Date.now()}.sym`);
  writeFileSync(tmpFile, [
    'al 00E000 .start',
    'al 00E010 .main_loop',
    'al 000400 .user_program',
  ].join('\n'));

  const syms = new Map();
  const labels = new Map();
  loadSymbols(tmpFile, syms, labels);

  // parseAddr with symbols
  assertEq(parseAddr('start', syms), 0xE000, 'resolve start');
  assertEq(parseAddr('main_loop', syms), 0xE010, 'resolve main_loop');
  assertEq(parseAddr('user_program', syms), 0x0400, 'resolve user_program');
  assertEq(parseAddr('$D000', syms), 0xD000, 'hex still works');
  assert(isNaN(parseAddr('nonexistent', syms)), 'missing label NaN');

  unlinkSync(tmpFile);
}

async function testMcpLoadBinary() {
  currentTest = 'mcp:n8_load_binary';

  // Create a temp binary
  const tmpBin = join(tmpdir(), `n8mcp_test_bin_${Date.now()}.bin`);
  const binData = Buffer.from([0xA9, 0x42, 0x8D, 0x00, 0x02, 0x60]);
  writeFileSync(tmpBin, binData);

  const mock = await mockWithHandshake(new Map([
    ['M', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  const data = readFileSync(tmpBin);
  await client.writeMemory(0xE000, data);

  assert(mock.received.some(r => r.startsWith('Me000,6:')), 'load binary sends M command');

  client.disconnect();
  mock.close();
  unlinkSync(tmpBin);
}

async function testMonitorCommand() {
  currentTest = 'mcp:monitor_command';
  const mock = await mockWithHandshake(new Map([
    ['qRcmd', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // Test kbd inject via monitor command
  const reply = await client.monitorCommand('kbd 41 00');
  assertEq(reply, 'OK', 'monitor command OK');

  const kbdHex = Buffer.from('kbd 41 00', 'utf8').toString('hex');
  assert(mock.received.some(r => r === `qRcmd,${kbdHex}`), 'sent correct qRcmd hex');

  client.disconnect();
  mock.close();
}

async function testMultiStepStop() {
  currentTest = 'mcp:multi_step_stop';
  // Server that returns T04 (SIGILL/jam) on second step
  let stepCount = 0;
  const server = createServer((socket) => {
    let buf = '';
    socket.on('data', (data) => {
      buf += data.toString('binary');
      while (buf.length > 0) {
        if (buf[0] === '+' || buf[0] === '-') { buf = buf.slice(1); continue; }
        const dollar = buf.indexOf('$');
        if (dollar === -1) { buf = ''; break; }
        if (dollar > 0) buf = buf.slice(dollar);
        const hash = buf.indexOf('#');
        if (hash === -1 || hash + 2 >= buf.length) break;
        const payload = buf.slice(1, hash);
        buf = buf.slice(hash + 3);
        socket.write('+', 'binary');
        if (payload.startsWith('qSupported')) socket.write(makePacket('PacketSize=4000'), 'binary');
        else if (payload === 'QStartNoAckMode') socket.write(makePacket('OK'), 'binary');
        else if (payload === '?') socket.write(makePacket('S05'), 'binary');
        else if (payload === 's') {
          stepCount++;
          socket.write(makePacket(stepCount < 3 ? 'T05thread:01;' : 'T04thread:01;'), 'binary');
        }
        else if (payload === 'g') socket.write(makePacket('420a14ff00d030'), 'binary');
      }
    });
  });

  const { port } = await new Promise(resolve => {
    server.listen(0, '127.0.0.1', () => resolve({ port: server.address().port }));
  });

  const client = new RspClient();
  await client.connect('127.0.0.1', port);

  // Step 5 times, should stop at step 3 (T04)
  let lastReply;
  let stepsCompleted = 0;
  for (let i = 0; i < 5; i++) {
    lastReply = await client.step();
    stepsCompleted++;
    if (!lastReply.startsWith('T05') && !lastReply.startsWith('S05')) break;
  }

  assertEq(stepsCompleted, 3, 'stopped after 3 steps');
  assert(lastReply.startsWith('T04'), 'got SIGILL');
  assertEq(fmtStop(lastReply), 'stopped signal 4', 'formatted as signal 4');

  client.disconnect();
  server.close();
}

// ── Run all ─────────────────────────────────────────────────────

async function main() {
  console.log('n8mcp tests\n');

  // Sync tests (shared modules)
  console.log('-- shared/address --');
  testParseAddr();

  console.log('-- shared/symbols --');
  testLoadSymbols();

  console.log('-- shared/format --');
  testHex8();
  testHex16();
  testHexdump();
  testFmtRegs();
  testFmtStop();

  console.log('-- shared/charmap --');
  testCharmap();

  console.log('-- shared/keyboard --');
  testCharToKeycode();
  testParseKeyInput();
  testNamedKeys();

  // Async tests (mock RSP)
  console.log('-- mcp tools (mock RSP) --');
  const asyncTests = [
    testMcpRegs,
    testMcpReadMemory,
    testMcpWriteMemory,
    testMcpWriteReg,
    testMcpStep,
    testMcpRun,
    testMcpHalt,
    testMcpReset,
    testMcpBreakpoints,
    testMcpClearAllBp,
    testMcpKbdInject,
    testMcpConsoleText,
    testMcpConsoleVideo,
    testMcpGoto,
    testAutoResume,
    testAutoResumeNotRunning,
    testSymbolAddressResolution,
    testMcpLoadBinary,
    testMonitorCommand,
    testMultiStepStop,
  ];

  for (const test of asyncTests) {
    try {
      await test();
    } catch (err) {
      failed++;
      console.error(`  FAIL (exception) [${test.name}]: ${err.message}`);
      if (process.env.N8GDB_DEBUG === '1') console.error(err.stack);
    }
  }

  console.log(`\n${passed} passed, ${failed} failed, ${passed + failed} total`);
  process.exit(failed > 0 ? 1 : 0);
}

main();
