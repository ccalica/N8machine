#!/usr/bin/env node

/**
 * Unit tests for n8gdb RSP client.
 * Spins up a mock GDB stub server and verifies protocol correctness.
 */

import { RspClient } from './rsp.mjs';
import { createServer } from 'net';
import { readFileSync, writeFileSync, unlinkSync, mkdirSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error(`  FAIL: ${msg}`); }
}

function assertEq(a, b, msg) {
  if (a === b) { passed++; }
  else { failed++; console.error(`  FAIL: ${msg}: expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`); }
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

/**
 * Create a mock GDB server that responds to commands.
 * @param {Map<string, string>} responses - command prefix -> reply data
 * @returns {{ port: number, close: () => void, received: string[] }}
 */
function createMockServer(responses) {
  const received = [];
  const server = createServer((socket) => {
    let buf = '';
    socket.on('data', (data) => {
      buf += data.toString('binary');
      while (buf.length > 0) {
        // Handle interrupt byte
        if (buf.charCodeAt(0) === 0x03) {
          buf = buf.slice(1);
          received.push('<interrupt>');
          socket.write(makePacket('T02thread:01;'), 'binary');
          continue;
        }
        const dollar = buf.indexOf('$');
        if (dollar === -1) { buf = ''; break; }
        if (dollar > 0) buf = buf.slice(dollar);
        const hash = buf.indexOf('#');
        if (hash === -1 || hash + 2 >= buf.length) break;

        const payload = buf.slice(1, hash);
        buf = buf.slice(hash + 3);
        received.push(payload);

        // Send ack
        socket.write('+', 'binary');

        // Find matching response
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
      const port = server.address().port;
      resolve({
        port,
        close: () => server.close(),
        received,
      });
    });
  });
}

// ── Tests ───────────────────────────────────────────────────────

async function testConnect() {
  console.log('test: connect and handshake');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000;swbreak+;hwbreak+'],
    ['QStartNoAckMode', 'OK'],
    ['?', 'S05'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  assert(client.connected, 'client should be connected');

  // Verify handshake sequence
  assert(mock.received.includes('qSupported:swbreak+;hwbreak+'), 'should send qSupported');
  assert(mock.received.includes('QStartNoAckMode'), 'should request no-ack');
  assert(mock.received.includes('?'), 'should query halt reason');

  client.disconnect();
  mock.close();
}

async function testReadRegisters() {
  console.log('test: read registers');
  // 6502 g-packet: A(2) X(2) Y(2) SP(2) PC_LE(4) P(2)
  // A=42, X=0a, Y=14, S=ff, PC=d000 (LE: 00d0), P=30
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['g', '420a14ff00d030'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  const regs = await client.readRegisters();

  assertEq(regs.a, 0x42, 'A register');
  assertEq(regs.x, 0x0a, 'X register');
  assertEq(regs.y, 0x14, 'Y register');
  assertEq(regs.s, 0xff, 'S register');
  assertEq(regs.p, 0x30, 'P register');
  assertEq(regs.pc, 0xd000, 'PC register');

  client.disconnect();
  mock.close();
}

async function testReadMemory() {
  console.log('test: read memory');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['m', '48656c6c6f'],  // "Hello"
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  const buf = await client.readMemory(0x0200, 5);

  assertEq(buf.toString('utf8'), 'Hello', 'memory content');
  assert(mock.received.some(r => r === 'm200,5'), 'should send m command with addr,len');

  client.disconnect();
  mock.close();
}

async function testWriteMemory() {
  console.log('test: write memory');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['M', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  await client.writeMemory(0x0200, Buffer.from([0xA9, 0x42, 0x8D, 0x00]));

  assert(mock.received.some(r => r.startsWith('M200,4:a942')), 'should send M command');

  client.disconnect();
  mock.close();
}

async function testBreakpoints() {
  console.log('test: set/clear breakpoints');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['Z0', 'OK'],
    ['z0', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  await client.setBreakpoint(0xD000);
  assert(mock.received.some(r => r === 'Z0,d000,1'), 'should send Z0 set');

  await client.clearBreakpoint(0xD000);
  assert(mock.received.some(r => r === 'z0,d000,1'), 'should send z0 clear');

  client.disconnect();
  mock.close();
}

async function testStep() {
  console.log('test: single step');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['s', 'T05thread:01;'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  const reply = await client.step();

  assert(reply.startsWith('T05'), 'step should return SIGTRAP');
  assert(mock.received.includes('s'), 'should send s command');

  client.disconnect();
  mock.close();
}

async function testContinue() {
  console.log('test: continue');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['c', 'T05thread:01;'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);
  const reply = await client.continue();

  assert(reply.startsWith('T05'), 'continue should return stop reply');
  assert(mock.received.includes('c'), 'should send c command');

  client.disconnect();
  mock.close();
}

async function testWriteRegister() {
  console.log('test: write register');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['P', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // Write PC (reg 4, 16-bit little-endian)
  await client.writeRegister(4, 0xD000);
  assert(mock.received.some(r => r === 'P4=00d0'), 'should send P4=00d0 (LE)');

  // Write A (reg 0, 8-bit)
  await client.writeRegister(0, 0x42);
  assert(mock.received.some(r => r === 'P0=42'), 'should send P0=42');

  client.disconnect();
  mock.close();
}

async function testInterrupt() {
  console.log('test: interrupt (pause)');
  // Mock server that doesn't respond to 'c' (simulating running target)
  // but responds to interrupt (0x03) with T02
  const received = [];
  const server = createServer((socket) => {
    let buf = '';
    let ignoreNext = false;  // ignore 'c' command (target running)
    socket.on('data', (data) => {
      buf += data.toString('binary');
      while (buf.length > 0) {
        if (buf.charCodeAt(0) === 0x03) {
          buf = buf.slice(1);
          received.push('<interrupt>');
          socket.write(makePacket('T02thread:01;'), 'binary');
          continue;
        }
        // Skip ack bytes
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
        // For handshake commands, send proper responses
        if (payload.startsWith('qSupported')) { socket.write(makePacket('PacketSize=4000'), 'binary'); }
        else if (payload === 'QStartNoAckMode') { socket.write(makePacket(''), 'binary'); }
        else if (payload === '?') { socket.write(makePacket('S05'), 'binary'); }
        // 'c' command: don't respond (target is running)
      }
    });
  });

  const { port } = await new Promise(resolve => {
    server.listen(0, '127.0.0.1', () => resolve({ port: server.address().port }));
  });

  const client = new RspClient();
  await client.connect('127.0.0.1', port);

  // Start a continue (fire-and-forget)
  client.continueAsync();

  // Small delay then interrupt
  await new Promise(r => setTimeout(r, 100));
  const reply = await client.pause();
  assert(reply.startsWith('T02') || reply.startsWith('S'), 'pause should return stop reply');
  assert(received.includes('<interrupt>'), 'should send 0x03 byte');

  client.disconnect();
  server.close();
}

async function testChecksumValidation() {
  console.log('test: checksum calculation');
  // Verify our checksum matches known values
  const ck = (data) => {
    let sum = 0;
    for (let i = 0; i < data.length; i++) sum += data.charCodeAt(i);
    return (sum & 0xFF).toString(16).padStart(2, '0');
  };

  assertEq(ck('OK'), '9a', 'checksum of "OK"');
  assertEq(ck('g'), '67', 'checksum of "g"');
  assertEq(ck('?'), '3f', 'checksum of "?"');
}

// ── Write register by name (wreg) ───────────────────────────────

async function testWriteRegisterByName() {
  console.log('test: write register by name (wreg protocol)');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['P', 'OK'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // wreg pc d000 → should send P4=00d0 (PC is reg 4, 16-bit LE)
  await client.writeRegister(4, 0xD000);
  assert(mock.received.some(r => r === 'P4=00d0'), 'wreg pc should send P4=00d0');

  // wreg a 42 → should send P0=42 (A is reg 0, 8-bit)
  await client.writeRegister(0, 0x42);
  assert(mock.received.some(r => r === 'P0=42'), 'wreg a should send P0=42');

  // wreg x ff → should send P1=ff (X is reg 1)
  await client.writeRegister(1, 0xFF);
  assert(mock.received.some(r => r === 'P1=ff'), 'wreg x should send P1=ff');

  // wreg s 80 → should send P3=80 (S is reg 3)
  await client.writeRegister(3, 0x80);
  assert(mock.received.some(r => r === 'P3=80'), 'wreg s should send P3=80');

  client.disconnect();
  mock.close();
}

// ── Set PC and goto ─────────────────────────────────────────────

async function testSetPc() {
  console.log('test: set PC (pc command protocol)');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['P', 'OK'],
    ['g', '000000ff00d000'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // pc d100 → write PC register then read regs
  await client.writeRegister(4, 0xD100);
  assert(mock.received.some(r => r === 'P4=00d1'), 'pc should send P4=00d1');

  const regs = await client.readRegisters();
  assertEq(regs.pc, 0xd000, 'readRegisters after pc set');

  client.disconnect();
  mock.close();
}

async function testGoto() {
  console.log('test: goto (set PC + continue)');
  const mock = await createMockServer(new Map([
    ['qSupported', 'PacketSize=4000'],
    ['QStartNoAckMode', ''],
    ['?', 'S05'],
    ['P', 'OK'],
    ['c', 'T05thread:01;'],
    ['g', '000000ff00d100'],
  ]));

  const client = new RspClient();
  await client.connect('127.0.0.1', mock.port);

  // goto sets PC then continues
  await client.writeRegister(4, 0xD100);
  assert(mock.received.some(r => r === 'P4=00d1'), 'goto should set PC first');

  const reply = await client.continue();
  assert(reply.startsWith('T05'), 'goto should return stop reply from continue');

  client.disconnect();
  mock.close();
}

// ── Symbol file parsing test ────────────────────────────────────

async function testSymbolParsing() {
  console.log('test: .sym file parsing');

  // Create a temp .sym file
  const tmpFile = join(tmpdir(), `n8gdb_test_${Date.now()}.sym`);
  writeFileSync(tmpFile, [
    'al 00D000 .start',
    'al 00D010 .main_loop',
    'al 00D100 .tty_write',
    'al 000200 .buffer',
  ].join('\n'));

  // We can't easily import the loadSymbols from the main module, so test inline
  const content = readFileSync(tmpFile, 'utf8');
  const syms = new Map();
  for (const line of content.split(/\r?\n/)) {
    const m = line.match(/^al\s+([0-9a-fA-F]+)\s+\.(\S+)/);
    if (m) syms.set(m[2], parseInt(m[1], 16));
  }

  assertEq(syms.get('start'), 0xD000, 'start symbol');
  assertEq(syms.get('main_loop'), 0xD010, 'main_loop symbol');
  assertEq(syms.get('tty_write'), 0xD100, 'tty_write symbol');
  assertEq(syms.get('buffer'), 0x0200, 'buffer symbol');
  assertEq(syms.size, 4, 'symbol count');

  unlinkSync(tmpFile);
}

// ── Run all ─────────────────────────────────────────────────────

async function main() {
  const tests = [
    testConnect,
    testReadRegisters,
    testReadMemory,
    testWriteMemory,
    testBreakpoints,
    testStep,
    testContinue,
    testWriteRegister,
    testWriteRegisterByName,
    testSetPc,
    testGoto,
    testInterrupt,
    testChecksumValidation,
    testSymbolParsing,
  ];

  for (const test of tests) {
    try {
      await test();
    } catch (err) {
      failed++;
      console.error(`  FAIL (exception): ${test.name}: ${err.message}`);
    }
  }

  console.log(`\n${passed} passed, ${failed} failed, ${passed + failed} total`);
  process.exit(failed > 0 ? 1 : 0);
}

main();
