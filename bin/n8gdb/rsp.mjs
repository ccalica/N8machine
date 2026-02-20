/**
 * GDB Remote Serial Protocol (RSP) client for 6502 targets.
 * Zero dependencies — uses only Node.js built-in 'net' module.
 *
 * Protocol reference: https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html
 * Adapted from mcp-winuae-emu gdb-protocol.ts (MIT, axewater)
 */

import { Socket } from 'net';

export class RspClient {
  /** @type {Socket|null} */  #socket = null;
  #pendingData = '';
  #noAckMode = false;
  #debug = false;
  /** @type {Array<{resolve: function, reject: function, timer: ReturnType<typeof setTimeout>}>} */
  #resolvers = [];
  #running = false;
  /** @type {string|null} */
  #pendingStop = null;
  /** @type {string} */
  #lastPacket = '';

  /**
   * @param {object} [opts]
   * @param {boolean} [opts.debug]
   */
  constructor(opts) {
    this.#debug = opts?.debug ?? (process.env.N8GDB_DEBUG === '1');
  }

  // ── Connection ──────────────────────────────────────────────

  /**
   * Connect to a GDB stub server and perform handshake.
   * @param {string} host
   * @param {number} port
   * @param {number} [timeoutMs=5000]
   */
  async connect(host, port, timeoutMs = 5000) {
    this.#socket = new Socket();

    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Connection timeout')), timeoutMs);
      this.#socket.connect(port, host, () => { clearTimeout(timer); resolve(); });
      this.#socket.on('error', (err) => { clearTimeout(timer); reject(err); });
    });

    this.#socket.on('data', (buf) => this.#handleData(buf));
    this.#socket.on('error', (err) => {
      this.#log(`Socket error: ${err.message}`);
      this.#rejectAll(new Error(`Socket error: ${err.message}`));
    });
    this.#socket.on('close', () => {
      this.#log('Socket closed');
      this.#rejectAll(new Error('Socket closed'));
    });

    // Handshake
    const supported = await this.#sendCommand('qSupported:swbreak+;hwbreak+');
    this.#log(`qSupported: ${supported}`);

    // Try no-ack mode
    try {
      const reply = await this.#sendCommand('QStartNoAckMode');
      if (reply === 'OK') {
        this.#noAckMode = true;
        this.#log('No-ack mode enabled');
      }
    } catch { /* not supported, continue with acks */ }

    // Query halt reason
    const reason = await this.#sendCommand('?');
    this.#log(`Halt reason: ${reason}`);
  }

  disconnect() {
    this.#rejectAll(new Error('Disconnected'));
    if (this.#socket) { this.#socket.destroy(); this.#socket = null; }
  }

  get connected() { return this.#socket !== null && !this.#socket.destroyed; }
  get isRunning() { return this.#running; }

  // ── Registers (6502: A, X, Y, S, P, PC) ────────────────────

  /**
   * Read all registers. Returns { a, x, y, s, p, pc }.
   * 6502 GDB register order: A(8), X(8), Y(8), S(8), P(8), PC(16) = 7 hex pairs + 4 hex chars = 18 hex chars
   */
  async readRegisters() {
    const reply = await this.#sendCommand('g');
    if (reply.length < 14) throw new Error(`Register reply too short: ${reply}`);
    // 6502 g-packet: A(2) X(2) Y(2) SP(2) PC_LE(4) P(2) = 14 hex chars
    // PC is little-endian: low byte at offset 8, high byte at offset 10
    const pcLo = parseInt(reply.slice(8, 10), 16);
    const pcHi = parseInt(reply.slice(10, 12), 16);
    return {
      a:  parseInt(reply.slice(0, 2), 16),
      x:  parseInt(reply.slice(2, 4), 16),
      y:  parseInt(reply.slice(4, 6), 16),
      s:  parseInt(reply.slice(6, 8), 16),
      pc: (pcHi << 8) | pcLo,
      p:  parseInt(reply.slice(12, 14), 16),
    };
  }

  /**
   * Read a single register by GDB index.
   * 6502: 0=A, 1=X, 2=Y, 3=S, 4=PC, 5=P
   * @param {number} id
   * @returns {Promise<number>}
   */
  async readRegister(id) {
    const reply = await this.#sendCommand(`p${id.toString(16)}`);
    if (id === 4) {
      // PC: 4 hex chars, little-endian
      const lo = parseInt(reply.slice(0, 2), 16);
      const hi = parseInt(reply.slice(2, 4), 16);
      return (hi << 8) | lo;
    }
    return parseInt(reply, 16);
  }

  /**
   * Write a single register.
   * @param {number} id
   * @param {number} value
   */
  async writeRegister(id, value) {
    let hex;
    if (id === 4) {
      // PC is 16-bit little-endian
      const v = value & 0xFFFF;
      hex = (v & 0xFF).toString(16).padStart(2, '0') +
            ((v >> 8) & 0xFF).toString(16).padStart(2, '0');
    } else {
      hex = (value & 0xFF).toString(16).padStart(2, '0');
    }
    const reply = await this.#sendCommand(`P${id.toString(16)}=${hex}`);
    if (reply !== 'OK') throw new Error(`Register write failed: ${reply}`);
  }

  // ── Memory ──────────────────────────────────────────────────

  /**
   * Read memory. Returns Buffer.
   * @param {number} addr
   * @param {number} length
   * @returns {Promise<Buffer>}
   */
  async readMemory(addr, length) {
    const reply = await this.#sendCommand(`m${addr.toString(16)},${length.toString(16)}`);
    if (reply.startsWith('E')) throw new Error(`Memory read error at $${addr.toString(16)}: ${reply}`);
    return Buffer.from(reply, 'hex');
  }

  /**
   * Write memory from a Buffer.
   * @param {number} addr
   * @param {Buffer} data
   */
  async writeMemory(addr, data) {
    const CHUNK = 256;
    for (let off = 0; off < data.length; off += CHUNK) {
      const chunk = data.subarray(off, Math.min(off + CHUNK, data.length));
      const hex = chunk.toString('hex');
      const reply = await this.#sendCommand(
        `M${(addr + off).toString(16)},${chunk.length.toString(16)}:${hex}`,
        30000
      );
      if (reply !== 'OK') throw new Error(`Memory write error at $${(addr + off).toString(16)}: ${reply}`);
    }
  }

  // ── Breakpoints ─────────────────────────────────────────────

  /** @param {number} addr */
  async setBreakpoint(addr) {
    const reply = await this.#sendCommand(`Z0,${addr.toString(16)},1`);
    if (reply !== 'OK') throw new Error(`Set breakpoint failed at $${addr.toString(16)}: ${reply}`);
  }

  /** @param {number} addr */
  async clearBreakpoint(addr) {
    const reply = await this.#sendCommand(`z0,${addr.toString(16)},1`);
    if (reply !== 'OK') throw new Error(`Clear breakpoint failed at $${addr.toString(16)}: ${reply}`);
  }

  // ── Execution Control ──────────────────────────────────────

  /**
   * Continue execution. Waits for stop reply.
   * @param {number} [timeoutMs=30000]
   * @returns {Promise<string>} stop reply packet
   */
  async continue(timeoutMs = 30000) {
    this.#pendingStop = null;
    this.#running = true;
    const reply = await this.#sendCommand('c', timeoutMs);
    this.#running = false;
    return reply;
  }

  /**
   * Continue without waiting. Returns immediately.
   * Use pause() or waitStop() to get the stop reply later.
   */
  continueAsync() {
    this.#pendingStop = null;
    this.#running = true;
    this.#sendPacket('c');
  }

  /**
   * Single step. Returns stop reply.
   * @returns {Promise<string>}
   */
  async step() {
    this.#running = true;
    const reply = await this.#sendCommand('s');
    this.#running = false;
    return reply;
  }

  /**
   * Send interrupt (0x03). Returns stop reply.
   * @returns {Promise<string>}
   */
  async pause() {
    if (this.#pendingStop) {
      const r = this.#pendingStop;
      this.#pendingStop = null;
      return r;
    }
    if (!this.#running) return 'S00';
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.#resolvers.findIndex(r => r.resolve === resolve);
        if (idx >= 0) this.#resolvers.splice(idx, 1);
        reject(new Error('Pause timeout'));
      }, 10000);
      this.#resolvers.push({ resolve, reject, timer });
      if (this.#socket && !this.#socket.destroyed) {
        this.#socket.write(Buffer.from([0x03]));
      }
    });
  }

  /**
   * Detach from target.
   */
  async detach() {
    try {
      await this.#sendCommand('D', 3000);
    } catch { /* ignore timeout on detach */ }
    this.disconnect();
  }

  // ── Internals ───────────────────────────────────────────────

  /** @param {Buffer} buf */
  #handleData(buf) {
    this.#pendingData += buf.toString('binary');

    while (this.#pendingData.length > 0) {
      if (this.#pendingData[0] === '+') { this.#pendingData = this.#pendingData.slice(1); continue; }
      if (this.#pendingData[0] === '-') {
        this.#log('NACK received, retransmitting');
        if (this.#lastPacket) this.#write(this.#lastPacket);
        this.#pendingData = this.#pendingData.slice(1);
        continue;
      }

      const dollar = this.#pendingData.indexOf('$');
      if (dollar === -1) { this.#pendingData = ''; break; }
      if (dollar > 0) this.#pendingData = this.#pendingData.slice(dollar);

      const hash = this.#pendingData.indexOf('#');
      if (hash === -1 || hash + 2 >= this.#pendingData.length) break;  // incomplete

      const payload = this.#pendingData.slice(1, hash);
      const ckStr = this.#pendingData.slice(hash + 1, hash + 3);
      this.#pendingData = this.#pendingData.slice(hash + 3);

      const expected = parseInt(ckStr, 16);
      const actual = this.#checksum(payload);
      if (expected !== actual) {
        this.#log(`Checksum mismatch: expected ${expected}, got ${actual}`);
        if (!this.#noAckMode) this.#write('-');
        continue;
      }
      if (!this.#noAckMode) this.#write('+');

      const decoded = this.#unescape(payload);
      this.#log(`<< ${decoded.slice(0, 120)}${decoded.length > 120 ? '...' : ''}`);

      // O packets — server console output (but NOT 'OK' which is an ack)
      if (decoded.startsWith('O') && decoded !== 'OK') {
        try { console.error(`[server] ${Buffer.from(decoded.slice(1), 'hex').toString('utf8').trim()}`); }
        catch { this.#log(`Server output: ${decoded.slice(1, 50)}`); }
        continue;
      }

      // Async stop replies
      if ((decoded.startsWith('S') || decoded.startsWith('T')) && this.#resolvers.length === 0) {
        this.#pendingStop = decoded;
        this.#running = false;
        this.#log(`Async stop: ${decoded}`);
        continue;
      }

      const resolver = this.#resolvers.shift();
      if (resolver) {
        clearTimeout(resolver.timer);
        if (decoded.startsWith('S') || decoded.startsWith('T')) this.#running = false;
        resolver.resolve(decoded);
      } else {
        this.#log(`Unsolicited: ${decoded.slice(0, 50)}`);
      }
    }
  }

  /** @param {string} data */
  #checksum(data) {
    let sum = 0;
    for (let i = 0; i < data.length; i++) sum += data.charCodeAt(i);
    return sum & 0xFF;
  }

  /** RSP escape: encode }, #, $, * as } followed by char XOR 0x20. */
  #escape(data) {
    let out = '';
    for (let i = 0; i < data.length; i++) {
      const c = data.charCodeAt(i);
      if (c === 0x7d || c === 0x23 || c === 0x24 || c === 0x2a) {
        out += '}' + String.fromCharCode(c ^ 0x20);
      } else {
        out += data[i];
      }
    }
    return out;
  }

  /** RSP unescape: decode } sequences. */
  #unescape(data) {
    let out = '';
    for (let i = 0; i < data.length; i++) {
      if (data[i] === '}' && i + 1 < data.length) {
        out += String.fromCharCode(data.charCodeAt(i + 1) ^ 0x20);
        i++;
      } else {
        out += data[i];
      }
    }
    return out;
  }

  /** @param {string} data */
  #sendPacket(data) {
    const escaped = this.#escape(data);
    const ck = this.#checksum(escaped).toString(16).padStart(2, '0');
    const pkt = `$${escaped}#${ck}`;
    this.#lastPacket = pkt;
    this.#log(`>> ${data.slice(0, 120)}${data.length > 120 ? '...' : ''}`);
    this.#write(pkt);
  }

  /**
   * @param {string} cmd
   * @param {number} [timeoutMs=10000]
   * @returns {Promise<string>}
   */
  #sendCommand(cmd, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.#resolvers.findIndex(r => r.resolve === resolve);
        if (idx >= 0) this.#resolvers.splice(idx, 1);
        reject(new Error(`Timeout: ${cmd}`));
      }, timeoutMs);
      this.#resolvers.push({ resolve, reject, timer });
      this.#sendPacket(cmd);
    });
  }

  /** @param {string} data */
  #write(data) {
    if (this.#socket && !this.#socket.destroyed) this.#socket.write(data, 'binary');
  }

  /** @param {string} msg */
  #log(msg) {
    if (this.#debug) console.error(`[rsp] ${msg}`);
  }

  /** @param {Error} err */
  #rejectAll(err) {
    for (const r of this.#resolvers) { clearTimeout(r.timer); r.reject(err); }
    this.#resolvers = [];
  }
}
