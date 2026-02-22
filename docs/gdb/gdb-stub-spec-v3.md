# GDB Remote Serial Protocol Stub — Implementation Spec for N8machine (v3)

## Changes from v2

This section summarizes all changes made based on findings from the test harness implementation (see `docs/test-harness-spec-v2.md` and the `feature/test-harness` branch).

### Critical Fixes
| Change | Source | Rationale |
|--------|--------|-----------|
| `emulator_reset()` unsafe for GDB `reset` callback — loads files, crashes if missing | TH-BUG | `emulator_reset()` calls `emulator_loadrom()` (opens `N8firmware`) and `emu_labels_init()` (opens `.sym`). Both crash on missing files. GDB reset must use `M6502_RES` pin instead. |
| Initial halt alignment must use do-while pattern | TH-SYNC | After `m6502_init()`, `M6502_SYNC` is already asserted. A `while (!(pins & M6502_SYNC))` loop never executes. Must use `do { step(); } while (...)` or check-first pattern. |
| Memory Map XML: TTY device region is 16 bytes (0xC100-0xC10F), not 4 | TH-BUS | Bus decode mask `0xFFF0` at `emulator.cpp:142` matches 0xC100-0xC10F. Registers 4-15 are phantom (default case returns 0x00). |

### Important Updates
| Change | Source | Rationale |
|--------|--------|-----------|
| BUG-1 cross-reference: `tty_decode()` register 3 read UB on empty queue | TH-BUG-1 | `emu_tty.cpp:87-88`: `front()`/`pop()` without size check. Reinforces D17 (GDB must bypass bus decode). |
| `tty_inject_char()` / `tty_buff_count()` now exist in production code | TH-PROD | Added to `emu_tty.cpp/h` for test harness. Available for GDB stub testing. |
| `IRQ_CLR()` zeroes `mem[0x00FF]` every tick | TH-IRQ | `emulator.cpp:92`. GDB reading IRQ state via `mem[]` sees 0x00 between assertions. Document as known limitation. |
| Breakpoint check does NOT gate on M6502_SYNC | TH-BP | `emulator.cpp:86` fires on ANY address match, not just instruction fetch. D32 (SYNC fix) is still needed. |
| GDB stub tests leverage doctest infrastructure | TH-TEST | Reuse test harness framework, fixtures, stubs, and `make test` pipeline. |
| `emulator_check_break()` clears `bp_hit` — confirmed in source | TH-BP2 | `emulator.cpp:158-163`. Two-consumer race (D29/SW-10.3) validated by test harness T97/T98. |

### New Decisions
| ID | Decision | Source | Rationale |
|----|----------|--------|-----------|
| D46 | GDB stub tests use doctest framework from test harness | TH | Single test binary, proven infrastructure, consistent with project conventions |
| D47 | GDB reset callback uses M6502_RES pin, not `emulator_reset()` | TH-BUG | `emulator_reset()` has fatal file dependencies. Safe reset: set RES pin + tick. |
| D48 | `tty_inject_char()` available for GDB stub TTY testing | TH-PROD | Already in production code. Enables testing GDB reads of TTY state. |
| D49 | Initial halt alignment handles SYNC-already-set after init | TH-SYNC | Use check-first or do-while pattern for `on_gdb_connect()`. |

---

## Changes from v1

This section summarizes all changes made based on the Hardware and Software critiques.

### Critical Fixes
| Change | Source | Rationale |
|--------|--------|-----------|
| Target XML `<architecture>` changed from `6502` to omitted; feature name changed to `org.n8machine.cpu` | SW-1.1 | GDB has no built-in "6502" architecture; unknown string may cause connection failure |
| `run_emulator` refactoring fully specified — moved from block-scoped static to file-scope with all reference sites enumerated | SW-2.1 | Variable is `static bool` inside main loop body; unreachable from callbacks without explicit refactoring |

### Important Fixes
| Change | Source | Rationale |
|--------|--------|-----------|
| Stop reply format changed to `T05thread:01;` | SW-1.6 | Modern GDB versions expect thread field; trivial to add |
| `bp_enable` shadowing resolved — GDB callback calls `emulator_enablebp(true)` | SW-2.2 | Two `bp_enable` variables (emulator.cpp:36 global, main.cpp:169 local) would cause silent breakpoint failure |
| Added `std::atomic<bool> interrupt_requested` for Ctrl-C fast path | SW-2.3 | Reduces Ctrl-C latency from frame-time (~16ms) to instruction-time |
| Added DISCONNECT command type | SW-2.4 | TCP thread needs to signal disconnect to main thread to clear `gdb_halted` |
| Use `shutdown(server_fd, SHUT_RDWR)` before `close()` | SW-2.5 | Closing fd from another thread while blocked in `accept()` is POSIX-undefined |
| Ctrl-C uses atomic flag, not command queue | SW-2.7 | Clarified: atomic flag checked in tick loop for fast response; INTERRUPT command only for poll return value |
| Added explicit state machine diagram | SW-4.1 | States were implicit; explicit diagram prevents ambiguity |
| Added input validation specs (non-hex, G packet length, address overflow) | SW-5.1-5.3 | Missing validation could cause silent corruption or undefined behavior |
| Poll semantics clarified: processes all queued commands, returns highest-priority state change | SW-7.1 | Single enum return needed clear semantics for multiple queued events |
| Separate GDB and ImGui breakpoint checks (no `bp_hit` clearing race) | SW-10.3 | `emulator_check_break()` clears `bp_hit` as side effect; two consumers race |
| Force `bp_enable=true` when GDB connected | SW-10.6 | ImGui checkbox could silently disable GDB breakpoints |
| `-lpthread` changed to `-pthread` | SW-9.1 | GCC requires `-pthread` (compiler+linker flag) for `std::thread` |
| Added `qXfer:memory-map:read` support | HW-8 | Simple, useful for both emulator and future hardware port |
| Added minimal `qRcmd` with `reset` command | HW-14 | Low cost; useful for scripted debug sessions |
| Prefetch sequence preserves IRQ/NMI pin state | HW-13 | `pins = M6502_SYNC` was zeroing IRQ/NMI — real bug |

### Minor/Deferred
| Change | Source | Rationale |
|--------|--------|-----------|
| Watchpoints (Z2/Z3/Z4) noted as future work | HW-7 | Not needed for emulator-first; useful for hardware port |
| Transport/protocol layer separation noted | HW-9 | Documents that Part 1 sections 1-2 are transport-specific |
| `?` query returns persisted stop reason | SW-1.8 | `last_stop_signal` must survive across commands |
| `vCont` remains unsupported (D11 unchanged) | SW-1.9 | GDB fallback to `s`/`c` is reliable; revisit if compatibility issues arise |
| `qXfer` offset/length documented as hex | SW-10.5 | Clarification |
| Hex output is lowercase | SW-10.4 | Convention documented |
| `Z`/`z` kind field accepted and ignored, documented | SW-5.4 | Clarification |
| `reset()` callback documents RES pin timing | SW-3.4 | Clarification |
| Detach clears all GDB-set breakpoints | SW-10.7 | Clean semantics |
| Emscripten note added | SW-9.2 | Must use `-DENABLE_GDB_STUB=0` |
| Added all new test cases from both critiques | HW-11, SW-6.x | Coverage gaps filled |
| Hardware port concerns collected in Appendix A | HW-1,2,3,4,9,10,12 | Documented for future reference without complicating emulator implementation |

---

## Reconciled Design Decisions

All decisions were evaluated across 3 independent agents (A: Protocol, B: Emulator Integration, C: Architecture) and refined by two critique rounds (HW: Hardware, SW: Software) and one test-harness-informed update round (TH).

| ID | Decision | Rationale | Agreement |
|----|----------|-----------|-----------|
| D1 | Bind to localhost only (127.0.0.1) | Security; no remote debug needed yet | A,C |
| D2 | Single client, kernel backlog queuing | Simplicity | A,C |
| D3 | `std::mutex` + `std::queue` for command/response queues | C++11 compatible, low throughput makes lock-free unnecessary | A,B,C |
| D4 | TCP thread blocks waiting for response | RSP is synchronous; no pipelining | A,B |
| D5 | `std::atomic<bool>` for shutdown flag | Cross-thread signal without queue overhead | A,C |
| D6 | Support `QStartNoAckMode` | Modern GDB expects it; eliminates round-trips | A |
| D7 | Dynamic `std::string` packet buffer, `PacketSize=20000` | Handles 64KB memory reads safely | A |
| D8 | Implement `p`/`P` for single register access | Minimal cost, avoids full register round-trips | A,B |
| D9 | "Step" = one instruction, not one tick | GDB expects instruction-level granularity; loop until M6502_SYNC | A,B |
| D10 | Map Z0 and Z1 to same `bp_mask[]` mechanism | No HW/SW breakpoint distinction on 6502 | A,B |
| D11 | Do not implement `vCont` | No benefit for single-threaded target; GDB falls back to `s`/`c` | A |
| D12 | `k` resumes emulator, does not exit | User keeps ImGui window; debugger-only disconnect | A,B,C |
| D13 | Target XML with custom feature, 6 registers (no architecture tag) | GDB can read/write registers; no native disassembly. Architecture tag omitted because GDB has no built-in 6502 support. **(v2: changed per SW-1.1)** | A,B,SW |
| D14 | Expose emulator state via accessor functions in `emulator.h` | Avoids 143KB m6502.h include leak, keeps CPU struct private | B,C |
| D15 | PC writes only valid at instruction boundary | GDB only writes registers when target is stopped; halt/step always land on boundaries | B |
| D16 | Use full prefetch sequence for GDB PC writes, preserving IRQ/NMI | Safe regardless of internal CPU state. **(v2: preserves existing IRQ/NMI pin state per HW-13)** **(v3: validated by test harness boot sequence testing)** | B,HW,TH |
| D17 | GDB memory reads bypass bus decode; direct `mem[]` access | Non-destructive reads; avoids TTY queue corruption. **(v3: reinforced by BUG-1 — `tty_decode()` empty queue UB makes bus-decode reads at $C103 hazardous)** | B,C,TH |
| D18 | GDB memory writes are plain `mem[addr] = val`, no device overlay sync | Simple, predictable. Document limitation for device-mapped regions. | B |
| D19 | Allow GDB writes to ROM area | Useful for patching; no write protection in hardware model | B |
| D20 | Detect instruction boundaries via `pins & M6502_SYNC` | Documented mechanism in m6502.h (line 124-125); more reliable than address-matching | B |
| D21 | Report JAM as SIGILL (signal 4) instead of SIGTRAP | Distinguishes illegal instruction from normal step completion | B |
| D22 | Step guard counter = 16 cycles | Covers worst case: 7-cycle instruction + 7-cycle interrupt vectoring + margin | B |
| D23 | Advance to instruction boundary on initial GDB halt | Ensures clean state for all subsequent GDB operations. **(v3: must handle SYNC-already-set after m6502_init — see D49)** | B,C,TH |
| D24 | Two files: `gdb_stub.h` + `gdb_stub.cpp` | Matches codebase convention. Split to three if >800 lines. | C |
| D25 | C++ API, not `extern "C"` | No C consumers exist; codebase is C++ throughout | C |
| D26 | Raw function pointers in callback struct | Matches `m6502_desc_t` style; zero overhead; C++11 compatible | B,C |
| D27 | `gdb_stub_poll()` returns an enum | Main loop needs to know what happened; enum is simplest | C |
| D28 | Runtime `config.enabled` + compile-time `ENABLE_GDB_STUB` | Runtime for convenience, compile-time for zero footprint | C |
| D29 | `gdb_stub_notify_stop(signal)` for breakpoint-during-run | Main thread pushes stop replies to TCP thread via response queue | B,C |
| D30 | GDB has priority over ImGui when connected | Run/Step gated by `gdb_halted` flag; ImGui controls disabled | B,C |
| D31 | Three-state status: Running / Halted / Halted (GDB) | User needs to understand why controls are unresponsive | C |
| D32 | Breakpoint check requires M6502_SYNC pin (instruction fetch only) | GDB execution breakpoints should not trigger on data access. Correctness fix for both GDB and existing debugger. **(v3: confirmed needed — current `emulator.cpp:86` has no SYNC gate)** | B,TH |
| D33 | `gdb_stub_poll()` early-return on no client (single atomic load) | Zero overhead when no GDB client connected | C |
| D34 | Reconnection resets all protocol state; new connect halts emulator | Clean semantics; no stale state confusion | A,C |
| D35 | `gdb_stub.cpp` has zero N8machine includes | All emulator access through callbacks; stub is reusable | C |
| D36 | Linux/macOS only initially; Windows Winsock deferred | Codebase is Linux-focused | C |
| D37 | Top-level try/catch in TCP thread | Thread must never crash; emulator unaffected by stub failure | C |
| D38 | Minimal ImGui: status text in existing window + console log | No new ImGui window; consistent with codebase UI style | C |
| D39 | Devices tick normally during GDB single-step | Preserves cycle accuracy; required for I/O instruction debugging | B |
| D40 | IRQ state frozen while halted, resumes on continue | Natural consequence of not calling m6502_tick() | B |
| D41 | TTY does not tick when halted | No emulated I/O when CPU stopped; correct behavior | B |
| D42 | **(v2)** `run_emulator` moved to file scope in main.cpp | Block-scoped static unreachable from GDB callbacks; explicit refactoring required | SW |
| D43 | **(v2)** GDB forces `bp_enable=true` when connected | Prevents ImGui checkbox from silently disabling GDB breakpoints | SW |
| D44 | **(v2)** Detach/disconnect clears all GDB-set breakpoints | Clean state on disconnect; emulator returns to user control | SW |
| D45 | **(v2)** `std::atomic<bool> interrupt_requested` for Ctrl-C fast path | Checked in tick loop between instructions; reduces latency to instruction-time | SW |
| D46 | **(v3)** GDB stub tests use doctest framework from test harness | Single test binary (`n8_test`), proven infrastructure, consistent with project conventions. Test suite: `"gdb_stub"`. | TH |
| D47 | **(v3)** GDB reset callback uses M6502_RES pin, not `emulator_reset()` | `emulator_reset()` calls `emulator_loadrom()` and `emu_labels_init()` which crash on missing files. Safe alternative: set RES pin + tick through reset sequence. | TH |
| D48 | **(v3)** `tty_inject_char()` / `tty_buff_count()` available for GDB stub testing | Already added to production code (`emu_tty.cpp/h`) by test harness. Enables testing TTY/IRQ interactions with GDB. | TH |
| D49 | **(v3)** Initial halt alignment handles SYNC-already-set | After `m6502_init()`, `M6502_SYNC` is set on pins. Alignment code must use do-while or check-first pattern, not plain while-loop. | TH |

---

## Part 1: Protocol & Network Layer

> **Transport/Protocol Separation Note** (per HW-9): Sections 1-2 of this part are TCP-specific transport. Section 3 (packet framing) and Section 4 (command parsing) are transport-independent and port directly to UART or other transports. A hardware port replaces Sections 1-2 only.

### 1. TCP Socket Layer

#### 1.1 Server Socket Setup

Default port: **3333** (configurable via `gdb_stub_config_t.port`).

Setup sequence:
1. `socket(AF_INET, SOCK_STREAM, 0)`
2. `setsockopt(SO_REUSEADDR)` — allow quick rebind after emulator restart
3. `bind(INADDR_LOOPBACK, port)` — localhost only [D1]
4. `listen(fd, 1)` — backlog of 1
5. `accept()` — blocks on the TCP thread

#### 1.2 Connection Model

Single client only [D2]. While a client is connected, `accept()` is not called again. Second connections queue in kernel backlog (backlog=1).

On disconnect:
1. Close client socket
2. Enqueue DISCONNECT command to main thread [D44]
3. Resume emulator execution (clear `gdb_halted`, clear GDB breakpoints)
4. Return to `accept()` for new connection

#### 1.3 Socket I/O Mode

Client socket uses **blocking reads with timeout** (`SO_RCVTIMEO` = ~100ms):
- Wakes periodically to check shutdown flag
- Writes are blocking (GDB reads promptly)

#### 1.4 Disconnect Detection

- `recv()` returns 0 (orderly shutdown)
- `recv()` returns -1 with `ECONNRESET` or `EPIPE`
- GDB sends `D` (detach) or `k` (kill)

On reconnect: emulator halts again, fresh protocol state [D34].

---

### 2. Hybrid Threading Architecture

#### 2.1 Thread Roles

**TCP Thread** (spawned at stub init):
- Owns server and client sockets
- `accept()`, `recv()`, `send()`
- Packet framing state machine
- ACK/NACK handling
- Enqueues parsed commands to command queue
- Dequeues responses from response queue, frames and sends
- Sets `interrupt_requested` atomic on Ctrl-C (0x03)

**Main Thread** (existing SDL/ImGui loop):
- Polls command queue once per frame via `gdb_stub_poll()` [D33]
- Executes commands via callback interface
- Enqueues responses to response queue
- Controls emulator run/halt state
- Checks `interrupt_requested` in tick loop between instructions [D45]

#### 2.2 Command Queue (Main Thread Inbox)

```cpp
struct GdbCommand {
    enum Type { QUERY, STEP, CONTINUE, READ_REG, WRITE_REG,
                READ_MEM, WRITE_MEM, SET_BP, CLEAR_BP,
                DETACH, KILL, INTERRUPT, DISCONNECT,  // v2: added DISCONNECT
                MONITOR_CMD, UNKNOWN };
    Type type;
    std::string raw_packet;  // full payload between $ and #
    uint16_t addr;
    uint16_t length;
    std::vector<uint8_t> data;
};
```

Protected by `std::mutex` + `std::queue` [D3]. Main thread drains queue each frame.

#### 2.3 Response Queue (TCP Thread Inbox)

`std::queue<std::string>` protected by `std::mutex` + `std::condition_variable`.

TCP thread calls `wait_for()` on condvar (timeout ~500ms) after enqueuing a command [D4].

**Latency budget**: worst-case single-command response latency is 500ms (condvar timeout). GDB's default `remotetimeout` is 2 seconds, providing adequate margin. In practice, response latency is sub-millisecond (next main loop frame).

#### 2.4 Continue/Step and Asynchronous Stop

After `c`/`s`:
- Main thread resumes emulator, does **not** enqueue response immediately
- TCP thread enters "waiting for stop" state
- Checks both response queue and socket (for Ctrl-C) using 100ms timeout
- Stop reply sent when breakpoint hit or step completes

#### 2.5 Ctrl-C Handling [D45]

Byte 0x03 detected by TCP thread at any time:
- TCP thread sets `std::atomic<bool> interrupt_requested = true`
- Also enqueues INTERRUPT command for `gdb_stub_poll()` return value
- If emulator halted (checked via atomic): ignored (no command enqueued)
- If emulator running: main thread tick loop checks `interrupt_requested` between instructions, halts promptly
- Stop reply: `T02thread:01;` (SIGINT)

**Clarification**: The atomic flag provides fast response (instruction-level latency). The INTERRUPT command in the queue is only for `gdb_stub_poll()` to return `GDB_POLL_HALTED` so the main loop state updates. The atomic flag is the primary mechanism; the command queue is for state bookkeeping.

#### 2.6 Shutdown Sequence

1. Main thread sets `std::atomic<bool> gdb_shutdown = true` [D5]
2. `shutdown(server_fd, SHUT_RDWR)` — portably unblocks `accept()` **(v2: per SW-2.5)**
3. `close(server_fd)`
4. TCP thread detects flag or `accept()` failure, closes client socket, exits
5. Main thread calls `std::thread::join()`

#### 2.7 Stub State Machine [v2: added per SW-4.1]

```
                    +-----------+
                    | NO_CLIENT |<---------+
                    +-----+-----+          |
                          |                |
                     accept()              |
                          |                |
                    +-----v-----+     disconnect/
                    |  HALTED   |<--+ detach/kill
                    +-----+-----+   |      |
                     /    |    \    |      |
                    /     |     \   |      |
              'c'  /   's' |  query |      |
                  /        |    \   |      |
           +-----v--+ +---v---+ |  |      |
           | RUNNING | |STEPPING| |  |      |
           +----+----+ +---+---+ |  |      |
                |          |     /  |      |
           bp/  |     step |    /   |      |
          Ctrl-C|   complete   /    |      |
                |          |  /     |      |
                +----+-----+-+      |      |
                     |              |      |
                     +--------------+      |
                     |                     |
                     +---------------------+

States:
  NO_CLIENT  — listening for connections
  HALTED     — CPU stopped, processing GDB commands
  RUNNING    — CPU executing, waiting for bp/Ctrl-C/disconnect
  STEPPING   — executing single instruction, auto-returns to HALTED

Transitions:
  NO_CLIENT -> HALTED     : accept() — halt emulator, align to SYNC [v3: see D49]
  HALTED -> RUNNING       : 'c' command
  HALTED -> STEPPING      : 's' command
  RUNNING -> HALTED       : breakpoint hit, Ctrl-C, or INTERRUPT
  STEPPING -> HALTED      : step complete (always)
  HALTED -> NO_CLIENT     : 'D' (detach), 'k' (kill), disconnect
  RUNNING -> NO_CLIENT    : disconnect during continue

Invalid in RUNNING state: register/memory commands (RSP is synchronous;
  GDB waits for stop reply before sending next command)
```

---

### 3. GDB RSP Packet Framing

#### 3.1 Packet Format

`$<data>#<two-hex-digit-checksum>`

Checksum = sum of all bytes in `<data>` mod 256.

#### 3.2 Parsing State Machine

```
States: IDLE -> PACKET_DATA -> CHECKSUM_1 -> CHECKSUM_2

IDLE + '$'  -> PACKET_DATA (reset buffer)
IDLE + 0x03 -> emit INTERRUPT (Ctrl-C) — set atomic flag
IDLE + '+'  -> ACK received (ignore in NoAckMode)
IDLE + '-'  -> NACK (retransmit last packet)

PACKET_DATA + '#'  -> CHECKSUM_1
PACKET_DATA + '}'  -> set escape flag (next byte XOR 0x20)
PACKET_DATA + byte -> append to buffer (including 0x03 — see T05)

CHECKSUM_1 + hex -> store, -> CHECKSUM_2
CHECKSUM_2 + hex -> validate checksum
```

Valid checksum -> send `+`, dispatch. Invalid -> send `-`, discard.

#### 3.3 NoAckMode

Supported via `QStartNoAckMode` [D6]. After `OK` response, both sides stop ACK/NACK.

#### 3.4 Escape Handling

Bytes `$`, `#`, `}`, `*` escaped as: `}` prefix + (byte XOR 0x20).

Note: Checksum is computed over the raw (escaped) byte stream, not the decoded bytes.

#### 3.5 Packet Buffer Sizing

Largest packet: 64KB memory read = 131072 hex chars + framing = ~131077 bytes.

Use `std::string` (dynamic) [D7]. Call `reserve(131072)` at init for the response buffer. Report `PacketSize=20000` (hex for 131072) in `qSupported`.

---

### 4. Command Parser & Dispatcher

All hex output uses **lowercase** characters (a-f).

| Prefix | Command | Response |
|--------|---------|----------|
| `?` | Stop reason | `T05thread:01;` (SIGTRAP) or persisted stop reason |
| `g` | Read all registers | 14 hex chars: `AAXXYYSSPPPPFF` |
| `G` | Write all registers | `OK` or `E03` if wrong length |
| `p n` | Read register n | 2 or 4 hex chars |
| `P n=v` | Write register n | `OK` |
| `m addr,len` | Read memory | Hex-encoded bytes |
| `M addr,len:data` | Write memory | `OK` |
| `s [addr]` | Single step | `T05thread:01;` (after instruction completes) |
| `c [addr]` | Continue | `T05thread:01;` on breakpoint, `T02thread:01;` on Ctrl-C |
| `Z0,addr,kind` | Set SW breakpoint | `OK` |
| `z0,addr,kind` | Clear SW breakpoint | `OK` |
| `Z1,addr,kind` | Set HW breakpoint | `OK` (same as Z0) [D10] |
| `z1,addr,kind` | Clear HW breakpoint | `OK` (same as z0) [D10] |
| `Z2`-`Z4` | Watchpoints | empty (unsupported; see Future Work) |
| `D` | Detach | `OK`, clear GDB BPs, resume, disconnect |
| `k` | Kill | Resume, disconnect [D12] |
| `Hg`/`Hc` | Thread select | `OK` (single-threaded) |
| `qSupported` | Capabilities | see Section 4.1 |
| `qAttached` | Attached query | `1` |
| `qXfer:features:read:target.xml:off,len` | Target XML | Chunked XML |
| `qXfer:memory-map:read::off,len` | Memory map XML | Chunked XML **(v2)** |
| `qfThreadInfo` | Thread list start | `m01` |
| `qsThreadInfo` | Thread list cont | `l` |
| `qC` | Current thread | `QC01` |
| `qRcmd,<hex>` | Monitor command | Hex-encoded output or `OK` **(v2)** |
| `vMustReplyEmpty` | Unknown v-packet | empty |
| `vCont?` | vCont query | empty [D11] |
| unknown | Unknown command | empty (`$#00`) |

#### 4.1 qSupported Response

```
PacketSize=20000;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+
```

#### 4.2 qRcmd (Monitor Commands) [v2]

GDB sends `qRcmd,<hex-encoded-command>`. Decode the hex string to get the command.

Supported commands:
| Command | Action |
|---------|--------|
| `reset` | Call `reset()` callback. **(v3: uses M6502_RES pin — see D47)**. Respond with hex-encoded "Reset\n" then `OK`. |
| (unknown) | Respond with hex-encoded "Unknown monitor command\n" then `OK`. |

Response format: `O<hex-encoded-output>` for output lines, then `OK`.

A `qRcmd` callback pointer in `gdb_stub_callbacks_t` allows future extensibility.

#### 4.3 Error Responses

| Code | Meaning |
|------|---------|
| `E01` | Bad address / memory access error (addr+len > 0x10000) |
| `E02` | Bad register number (>5) |
| `E03` | Malformed command / parse error |

#### 4.4 Input Validation [v2: added per SW-5.1-5.3]

All command parsers MUST validate:

1. **Non-hex characters**: Any non-hex character in address, length, or data fields produces `E03`.
2. **Address overflow**: Parsed hex values for addresses are validated to fit `uint16_t` (0-0xFFFF). Values > 0xFFFF produce `E01`.
3. **Length overflow**: `addr + len > 0x10000` produces `E01`.
4. **`G` packet length**: Must be exactly 14 hex chars. Otherwise `E03`.
5. **`P` register value size**: 8-bit registers accept 2 hex chars max; PC accepts 4 hex chars max. Excess chars produce `E03`.
6. **`Z`/`z` kind field**: Accepted and ignored. The 6502 has no variable-length instructions for breakpoint purposes.

#### 4.5 Register Layout (GDB register numbers)

| Reg# | Name | Size | Encoding |
|------|------|------|----------|
| 0 | A | 8-bit | 2 hex chars |
| 1 | X | 8-bit | 2 hex chars |
| 2 | Y | 8-bit | 2 hex chars |
| 3 | SP | 8-bit | 2 hex chars |
| 4 | PC | 16-bit | 4 hex chars, little-endian |
| 5 | P (flags) | 8-bit | 2 hex chars |

`g` response example: A=0x42, X=0x00, Y=0xFF, SP=0xFD, PC=0xD000, P=0x24 -> `420000fffd00d024`

#### 4.6 Stop Reason Persistence [v2: per SW-1.8]

The stub maintains a `last_stop_signal` variable (default: 5/SIGTRAP). Updated by:
- Step completion: 5 (SIGTRAP) or 4 (SIGILL for JAM)
- Breakpoint hit: 5 (SIGTRAP)
- Ctrl-C: 2 (SIGINT)
- Connect: 5 (SIGTRAP)

The `?` query always returns `T<signal>thread:01;` using `last_stop_signal`.

---

### 5. 6502 Target Description XML [D13, v2]

```xml
<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target version="1.0">
  <feature name="org.n8machine.cpu">
    <reg name="a"     bitsize="8"  type="uint8"    regnum="0"/>
    <reg name="x"     bitsize="8"  type="uint8"    regnum="1"/>
    <reg name="y"     bitsize="8"  type="uint8"    regnum="2"/>
    <reg name="sp"    bitsize="8"  type="uint8"    regnum="3"/>
    <reg name="pc"    bitsize="16" type="code_ptr"  regnum="4"/>
    <reg name="flags" bitsize="8"  type="uint8"    regnum="5"/>
  </feature>
</target>
```

Changes from v1:
- **Removed** `<architecture>6502</architecture>` — GDB has no built-in 6502 architecture. An unknown architecture string may cause GDB to refuse the connection or disable features.
- **Changed** feature name from `org.gnu.gdb.m6502.cpu` to `org.n8machine.cpu` — the `org.gnu.gdb` prefix implies GDB recognition which does not exist.

SP is raw 8-bit (not 0x01xx). PC little-endian. GDB has no native 6502 disassembly.

Served via `qXfer:features:read:target.xml:offset,length` with `l`/`m` prefix for last/more chunks. **Note**: offset and length in `qXfer` requests are hex-encoded.

---

### 6. N8machine Memory Map XML [v2, updated v3]

```xml
<?xml version="1.0"?>
<!DOCTYPE memory-map SYSTEM "gdb-memory-map.dtd">
<memory-map>
  <memory type="ram"  start="0x0000" length="0xC000"/>
  <memory type="ram"  start="0xC000" length="0x0100">
    <!-- Frame buffer: $C000-$C0FF (256 bytes) -->
    <!-- Bus decode: BUS_DECODE(addr, 0xC000, 0xFF00) — emulator.cpp:127 -->
  </memory>
  <memory type="ram"  start="0xC100" length="0x0010">
    <!-- TTY device: $C100-$C10F (16 bytes, mask 0xFFF0) — emulator.cpp:142 -->
    <!-- Registers 0-3 are functional, 4-15 hit default case (phantom) -->
    <!-- v3: BUG-1: reading register 3 ($C103) via bus decode on empty queue is UB -->
    <!-- GDB reads return raw mem[], not device state (D17) -->
  </memory>
  <memory type="ram"  start="0xC110" length="0x0EF0"/>
  <memory type="rom"  start="0xD000" length="0x3000"/>
</memory-map>
```

**v3 changes from v2**:
- Split the `$C000-$C1FF` region into three precise sub-regions: frame buffer ($C000-$C0FF), TTY ($C100-$C10F), and unmapped ($C110-$CFFF).
- TTY region documented as 16 bytes (bus decode mask `0xFFF0`), not 4. The test harness (T78a) confirmed registers 4-15 exist as phantom addresses returning 0x00.
- `$C110` and above do NOT hit TTY decode (test harness T67a confirmed: `0xC110 & 0xFFF0 = 0xC110 != 0xC100`).
- BUG-1 hazard documented: reading $C103 via bus decode when TTY buffer is empty invokes UB (`front()` on empty `std::queue`).

Served via `qXfer:memory-map:read::offset,length` (empty annex name). Same `l`/`m` chunking as target XML.

Note: ROM is marked as `rom` type. GDB will prefer hardware breakpoints (Z1) for ROM regions. Since D10 maps Z1 to the same mechanism as Z0, this is transparent.

---

### 7. Packet Flow Examples

#### Initial Connection
```
GDB connects -> stub halts emulator, aligns to SYNC boundary [v3: D49]
GDB -> $qSupported:...#xx
Stub -> +$PacketSize=20000;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+#xx
GDB -> +$QStartNoAckMode#xx
Stub -> +$OK#xx
(no more ACKs)
GDB -> $qXfer:features:read:target.xml:0,fff#xx
Stub -> $l<?xml ...>#xx
GDB -> $?#xx
Stub -> $T05thread:01;#xx
GDB -> $Hg0#xx
Stub -> $OK#xx
GDB -> $g#xx
Stub -> $420000fffd00d024#xx
```

#### Set Breakpoint and Continue
```
GDB -> $Z0,d000,1#xx
Stub -> $OK#xx
GDB -> $c#xx
... emulator runs, hits BP at 0xD000 ...
Stub -> $T05thread:01;#xx
```

#### Ctrl-C Interrupt
```
GDB -> $c#xx
... emulator running ...
GDB -> 0x03 (raw byte)
... TCP thread sets interrupt_requested=true ...
... tick loop checks atomic, halts ...
Stub -> $T02thread:01;#xx (SIGINT)
```

#### Monitor Reset [v3: updated]
```
GDB -> $qRcmd,7265736574#xx    ("reset" hex-encoded)
Stub -> callback: sets M6502_RES pin, ticks through reset sequence [D47]
Stub -> $O52657365740a#xx       ("Reset\n" hex-encoded output)
Stub -> $OK#xx
```

---

## Part 2: Emulator Integration Layer

### 1. Callback Interface

The GDB stub communicates with N8machine exclusively through a callback struct [D26, D35]. The stub never includes N8machine headers or touches emulator globals directly.

#### 1.1 Callback Struct

```cpp
struct gdb_stub_callbacks_t {
    // Register access (by GDB register number 0-5)
    uint8_t  (*read_reg8)(int reg_num);       // regs 0-3,5
    uint16_t (*read_reg16)(int reg_num);      // reg 4 (PC)
    void     (*write_reg8)(int reg_num, uint8_t val);
    void     (*write_reg16)(int reg_num, uint16_t val);

    // Memory access (direct mem[] — bypasses bus decode) [D17]
    uint8_t  (*read_mem)(uint16_t addr);
    void     (*write_mem)(uint16_t addr, uint8_t val);

    // Execution control
    void     (*step_instruction)(void);   // one full instruction [D9]
    void     (*continue_exec)(void);      // resume free-running
    void     (*halt)(void);               // stop execution

    // Breakpoints
    int      (*set_breakpoint)(uint16_t addr);    // returns 0 on success
    int      (*clear_breakpoint)(uint16_t addr);

    // Status
    int      (*get_stop_reason)(void);    // GDB signal number
    uint16_t (*get_pc)(void);             // current PC

    // Reset [v3: MUST NOT call emulator_reset() — see D47]
    void     (*reset)(void);

    // Monitor command (optional, may be NULL)
    // Returns heap-allocated response string or NULL for unknown.
    // Caller frees.
    char*    (*monitor_cmd)(const char* cmd);
};
```

**Contract for `step_instruction()`** (per SW-3.1): Executes exactly one 6502 instruction. Sets internal `last_stop_signal` to SIGTRAP (5) on normal completion or SIGILL (4) on JAM. After return, CPU is at a SYNC boundary. Implementation details (guard counter, M6502_SYNC detection) are the callback's responsibility, not the stub's.

**Contract for `reset()`** (per SW-3.4, **updated v3 per D47**):

> **WARNING**: `emulator_reset()` is NOT safe for GDB use. It calls `emulator_loadrom()` (opens `"N8firmware"` file, crashes if missing — `emulator.cpp:58`) and `emu_labels_init()` (opens `.sym` file, calls `exit(-1)` if missing — `emu_labels.cpp:47`). In a debug session, the ROM is already loaded in `mem[]`.

The GDB `reset()` callback implementation MUST use the M6502_RES pin approach:
```cpp
void gdb_reset_callback() {
    // Assert RES pin (emulator_reset does this too, at line 260)
    pins = pins | M6502_RES;
    // Reset TTY state (safe — no file dependencies)
    tty_reset();
    // Do NOT call emulator_loadrom() or emu_labels_init()
    // ROM is already in mem[]. Labels are already loaded.
}
```

After the callback returns, the next `step_instruction()` or `continue_exec()` executes the reset sequence (RES pin causes CPU to read reset vector from $FFFC/$FFFD). PC will not immediately reflect the reset vector — the CPU needs ~7 ticks to complete the reset sequence.

#### 1.2 Callback -> Emulator Mapping

New accessor functions added to `emulator.h`/`emulator.cpp` [D14]:

| Callback | Emulator Implementation |
|----------|------------------------|
| `read_reg8(0)` | `emulator_read_a()` -> `m6502_a(&cpu)` |
| `read_reg8(1)` | `emulator_read_x()` -> `m6502_x(&cpu)` |
| `read_reg8(2)` | `emulator_read_y()` -> `m6502_y(&cpu)` |
| `read_reg8(3)` | `emulator_read_s()` -> `m6502_s(&cpu)` |
| `read_reg8(5)` | `emulator_read_p()` -> `m6502_p(&cpu)` |
| `read_reg16(4)` | `emulator_getpc()` -> `m6502_pc(&cpu)` (already exists) |
| `write_reg8(n)` | `emulator_write_a/x/y/s/p()` -> `m6502_set_a/x/y/s/p(&cpu, v)` |
| `write_reg16(4)` | `emulator_write_pc()` -> full prefetch sequence [D16] |
| `read_mem(addr)` | `return mem[addr]` — direct, no bus decode [D17] |
| `write_mem(addr, val)` | `mem[addr] = val` — direct, ROM writable [D18, D19] |
| `set_breakpoint(addr)` | `bp_mask[addr] = true; emulator_enablebp(true)` **(v2: calls function, not raw global)** |
| `clear_breakpoint(addr)` | `bp_mask[addr] = false` |
| `step_instruction()` | Loop `emulator_step()` until `pins & M6502_SYNC` [D20] |
| `continue_exec()` | Set `run_emulator = true` (file-scope) [D42] |
| `halt()` | Set `run_emulator = false` (file-scope) [D42] |
| `get_stop_reason()` | Return `last_stop_signal` (5=SIGTRAP or 4=SIGILL) [D21] |
| `get_pc()` | `emulator_getpc()` |
| `reset()` | **v3**: Set `pins |= M6502_RES`, call `tty_reset()`. Do NOT call `emulator_reset()`. [D47] |
| `monitor_cmd(cmd)` | Route through callback; default implementation handles "reset" |

### 2. Register Access

#### 2.1 m6502.h Getter/Setter Pairs (confirmed)

- Getters: `m6502_a()`, `m6502_x()`, `m6502_y()`, `m6502_s()`, `m6502_p()`, `m6502_pc()`
- Setters: `m6502_set_a()`, `m6502_set_x()`, `m6502_set_y()`, `m6502_set_s()`, `m6502_set_p()`, `m6502_set_pc()`

All setters directly write CPU struct fields. No side effects except PC.

#### 2.2 PC Write — Full Prefetch Sequence [D16, v2: preserves IRQ/NMI]

Writing PC requires resetting the fetch state machine while preserving interrupt pin state:
```cpp
void emulator_write_pc(uint16_t addr) {
    // v2: preserve existing IRQ/NMI pin state (HW-13 fix)
    pins = (pins & (M6502_IRQ | M6502_NMI)) | M6502_SYNC | M6502_RW;
    M6502_SET_ADDR(pins, addr);
    M6502_SET_DATA(pins, mem[addr]);
    m6502_set_pc(&cpu, addr);
}
```

This ensures the next `m6502_tick()` begins a clean opcode fetch regardless of prior CPU state, without clobbering active interrupt signals.

**(v3 note)**: The test harness validated that `M6502_SYNC` is set after `m6502_init()`, confirming the prefetch sequence correctly models the post-init state. The boot sequence in CpuFixture uses the same SYNC-based boundary detection.

#### 2.3 Flags Register (P)

Standard 6502 bit layout: `NV-BDIZC` (bits 7-0). Read/write atomically via `m6502_p()`/`m6502_set_p()`.

### 3. Memory Access

- Direct `mem[]` read/write — bypasses bus decode [D17]
- No device side-effects on read (TTY registers, frame buffer)
- ROM area ($D000+) writable — useful for debug patching [D19]
- Address range: `uint16_t` naturally constrains to 0x0000-0xFFFF
- No per-access validation needed — `mem[]` is exactly 65536 bytes
- **Range validation**: `addr + len > 0x10000` returns `E01`

**Bus decode notes** (updated v3):

1. **(per HW-12)**: `mem[0xC103]` contains the last CPU-written value, not current device state. Device read data overwrites `mem[]` only during a CPU bus read cycle. This is acceptable for GDB inspection but documented as a known limitation.

2. **(v3, per TH-BUG-1)**: `tty_decode()` register 3 read (`$C103`) calls `tty_buff.front()` and `tty_buff.pop()` unconditionally (`emu_tty.cpp:87-88`). If the TTY input queue is empty, `front()` on empty `std::queue` is **undefined behavior** (crash or garbage). This bug reinforces D17: GDB MUST bypass bus decode for memory reads. A bus-decode read at `$C103` when the buffer is empty would trigger UB.

3. **(v3, per TH-IRQ)**: `IRQ_CLR()` at `emulator.cpp:92` zeroes `mem[0x00FF]` every tick before device ticks re-assert IRQ bits. GDB reading `mem[0x00FF]` directly will see 0x00 unless a device has set a bit since the last tick. This is the intended interrupt register behavior (clear-on-tick, re-assert by devices), but means GDB cannot reliably inspect the "steady-state" IRQ value between ticks. The value is only meaningful during a bus-decode read cycle when the full clear/assert/check pipeline has executed.

### 4. Instruction-Level Stepping

#### 4.1 Instruction Boundary Detection [D20]

`m6502.h` signals boundaries via the `M6502_SYNC` pin. The `_FETCH()` macro at the end of each instruction sets SYNC. Verified in source:
- NOP (0xEA): 2 states, `_FETCH()` at state 1
- LDA abs (0xAD): 4 states, `_FETCH()` at state 3
- BRK (0x00): 7 states, `_FETCH()` at state 6

**(v3 note)**: The test harness validated SYNC timing for all tested instructions. CpuFixture's `run_until_sync()` uses a do-while loop with 100-tick safety limit, confirming the SYNC detection approach works correctly.

#### 4.2 Step Algorithm

```
step_instruction():
    // Precondition: halted at SYNC boundary (pins & M6502_SYNC)

    // Tick 1: consume SYNC, load opcode into IR, execute state 0
    emulator_step()

    // Tick 2..N: execute remaining states until _FETCH() sets SYNC
    guard = 0
    while !(pins & M6502_SYNC) && guard < 16:   // [D22]
        emulator_step()
        guard++

    if guard >= 16:
        last_stop_signal = 4   // SIGILL — JAM detected [D21]
    else:
        last_stop_signal = 5   // SIGTRAP — normal step
```

#### 4.3 Initial Halt Alignment [D23, v3: updated per D49]

When GDB first connects and the emulator is halted mid-instruction, advance to the next SYNC boundary before processing any GDB commands.

**v3 critical fix**: After `m6502_init()`, `M6502_SYNC` is already set on `pins`. A plain `while (!(pins & M6502_SYNC))` loop would never execute, leaving the CPU in an un-booted state. This was discovered and fixed in the test harness (CpuFixture's `boot()` was changed from `while` to `do-while`).

The alignment code must handle two cases:

```
on_gdb_connect():
    if (pins & M6502_SYNC):
        // Already at instruction boundary — no alignment needed.
        // This includes the post-m6502_init() state.
        return

    // Mid-instruction — advance to next boundary
    guard = 0
    do:
        emulator_step()
        guard++
    while !(pins & M6502_SYNC) && guard < 16
```

**Rationale for check-first pattern**: Unlike the step algorithm (which must consume the current SYNC and execute an instruction), halt alignment only needs to reach a boundary. If already at one, no ticking is needed. This differs from the test harness's `boot()` (which uses do-while because it needs to execute the reset sequence), because GDB connect alignment is about reaching a clean state, not executing an instruction.

#### 4.4 Edge Cases

- **BRK**: 7 cycles, completes normally, lands at IRQ handler. Report SIGTRAP.
- **JAM opcodes** (0x02, 0x12, etc.): Loop forever via `c->IR--`. Guard counter fires -> SIGILL.
- **Interrupt during step**: IRQ/NMI forces BRK sequence (7 extra cycles). Guard of 16 handles worst case.
- **NMI during step**: NMI has priority; step completes at NMI handler entry. Report SIGTRAP (not a special signal).

### 5. Continue and Breakpoint Detection

#### 5.1 Run State [D42 — v2 refactoring]

`run_emulator` must be moved from block-scoped `static bool` inside the main loop body (main.cpp line 167) to **file-scope** in main.cpp. This is required because GDB callbacks (`continue_exec`, `halt`) must read/write it.

**Refactoring steps**:
1. Move `static bool run_emulator = false;` from main loop body (line 167) to file scope (before `main()`)
2. Move `static bool step_emulator = false;` to file scope (same reason — needed for step callback)
3. Move `static bool bp_enable = false;` to file scope — **or eliminate it entirely** (see Section 5.4)
4. All existing references (Run/Pause button at line 219, tick check at line 174, breakpoint check at line 179) continue to work unchanged since the variable name and type are identical
5. Add `static bool gdb_halted = false;` at file scope (new)

**Variables at file scope after refactoring**:
```cpp
// main.cpp — file scope
static bool run_emulator = false;
static bool step_emulator = false;
static bool gdb_halted = false;      // NEW — GDB controls execution
```

#### 5.2 Breakpoint SYNC Fix [D32]

Modify `emulator_step()` breakpoint check to only fire on instruction fetch:
```cpp
// Before (fires on any memory access — emulator.cpp line 86):
if(bp_enable && bp_mask[addr]) { bp_hit = true; ... }

// After (fires only on opcode fetch):
if(bp_enable && bp_mask[addr] && (pins & M6502_SYNC)) { bp_hit = true; ... }
```

**(v3 note)**: The test harness confirmed that the current code at `emulator.cpp:86` does NOT gate on SYNC. Test T97 (breakpoint hit) passes because test programs happen to not have breakpoint addresses appear as data operands. This fix remains critical for correctness — a program like `LDA $D000` where a breakpoint is set at `$D000` would falsely trigger on the data read cycle.

This is a correctness fix for both GDB and the existing ImGui debugger.

#### 5.3 Stop Notification [D29] — Separate GDB and ImGui Checks [v2: per SW-10.3]

The existing `emulator_check_break()` clears `bp_hit` as a side effect. Two consumers (ImGui loop and GDB) would race on this. Fix:

```cpp
// emulator.cpp — separate the check from the clear
bool emulator_bp_hit() {
    return bp_enable && bp_hit;
}
void emulator_clear_bp_hit() {
    bp_hit = false;
}
```

**(v3 note)**: The test harness (T97, T98) validates the existing `emulator_check_break()` behavior: it returns true and clears `bp_hit` atomically. The split into `emulator_bp_hit()` + `emulator_clear_bp_hit()` is still required for the GDB two-consumer case.

**Main loop breakpoint handling (when GDB connected)**:
```cpp
if (run_emulator) {
    // ... tick loop ...
    if (emulator_bp_hit()) {
        run_emulator = false;
        emulator_clear_bp_hit();
        if (gdb_stub_is_connected()) {
            gdb_halted = true;
            gdb_stub_notify_stop(5);  // SIGTRAP
        }
    }
    // Also check interrupt_requested atomic
    if (gdb_interrupt_requested()) {
        run_emulator = false;
        gdb_halted = true;
        gdb_stub_notify_stop(2);  // SIGINT
    }
}
```

**Main loop breakpoint handling (when GDB not connected — original behavior)**:
```cpp
if (emulator_check_break()) { run_emulator = false; break; }
```

The original `emulator_check_break()` is kept for backward compatibility when GDB is not connected.

#### 5.4 `bp_enable` Shadowing Resolution [v2: D43, per SW-2.2, SW-10.6]

There are two `bp_enable` variables:
- `emulator.cpp` line 36: `bool bp_enable;` (global, controls actual breakpoint checking)
- `main.cpp` line 169: `static bool bp_enable = false;` (local, drives ImGui checkbox)

Resolution:
1. **Eliminate** the main.cpp local `bp_enable`. Use `emulator_enablebp()` / a new `emulator_bp_enabled()` getter instead.
2. The ImGui checkbox calls `emulator_enablebp()` directly (already does this at line 237).
3. **When GDB is connected** [D43]: `set_breakpoint()` callback calls `emulator_enablebp(true)`. The ImGui BP checkbox is disabled (grayed out) while GDB is connected, preventing the user from disabling breakpoints that GDB set.
4. On GDB disconnect: restore `bp_enable` to its pre-connection state or leave as-is (breakpoints from GDB session persist until cleared).

**(v3 note)**: The test harness validated `emulator_enablebp()` (T97 enables, T98 disables). The function works correctly as the sole control point for `bp_enable`.

### 6. Device Interaction During Debug

- **TTY**: Does not tick when halted. Ticks normally during step [D39, D41].
- **Frame buffer**: Frozen when halted. Updated during step.
- **IRQ**: Frozen while halted. Resumes on continue [D40].
- **stdin/TTY conflict**: None — GDB uses TCP, not stdin. Note: `tty_tick()` calls `select()` on stdin during step cycles; this may consume user keystrokes if the terminal is focused. Document as known behavior.

**(v3 notes from test harness)**:
- The test harness runs with `< /dev/null` to prevent `tty_tick()`/`select()` from blocking or consuming unexpected stdin data. GDB sessions don't need this (TCP transport), but if running headless tests against the GDB stub, the same `/dev/null` redirect should be used.
- `tty_inject_char()` (added for test harness) can be used in GDB stub tests to simulate TTY input without going through stdin/`tty_tick()`.
- IRQ assertion through `tty_tick()` is validated by test harness T101/T101a. The pipeline: `IRQ_CLR()` → `tty_tick()` (re-asserts if buffer non-empty) → IRQ pin check (`mem[0x00FF]`). This occurs every `emulator_step()`, including during GDB single-step [D39].

---

## Part 3: Architecture & API Design

### 1. File Organization [D24]

| File | Purpose |
|------|---------|
| `src/gdb_stub.h` | Public API header |
| `src/gdb_stub.cpp` | TCP thread, queues, packet framing, protocol handler |

Zero N8machine includes in `gdb_stub.cpp` [D35]. All emulator access through callbacks.

Split to 3 files only if implementation exceeds ~800 lines.

### 2. Public API

#### 2.1 Configuration and Lifecycle

```cpp
struct gdb_stub_config_t {
    uint16_t port;      // default: 3333
    bool     enabled;   // false = init is a no-op
};

int  gdb_stub_init(const gdb_stub_config_t* config,
                   const gdb_stub_callbacks_t* callbacks);
void gdb_stub_shutdown(void);
```

#### 2.2 Poll Function [D27]

```cpp
enum gdb_poll_result_t {
    GDB_POLL_NONE     = 0,  // no state change (commands may have been processed)
    GDB_POLL_HALTED   = 1,  // GDB requested halt (or client connected)
    GDB_POLL_RESUMED  = 2,  // GDB sent 'c' (continue)
    GDB_POLL_STEPPED  = 3,  // GDB sent 's' (step completed)
    GDB_POLL_DETACHED = 4,  // GDB detached/disconnected
    GDB_POLL_KILL     = 5,  // GDB sent 'k'
};

gdb_poll_result_t gdb_stub_poll(void);
```

Early-returns `GDB_POLL_NONE` if no client connected (single atomic load) [D33].

**Poll semantics** (v2: clarified per SW-7.1): `gdb_stub_poll()` drains the entire command queue, processing all pending commands (register reads, memory reads, breakpoint sets, etc.). It returns the **highest-priority state-changing event** encountered:
- Priority order: HALTED > KILL > DETACHED > STEPPED > RESUMED > NONE
- Non-state-changing commands (register read, memory read, query) are processed but do not affect the return value.
- The caller does NOT need to loop until `GDB_POLL_NONE` — one call per frame is sufficient.

#### 2.3 Status and Notification

```cpp
bool gdb_stub_is_connected(void);
bool gdb_stub_is_halted(void);
void gdb_stub_notify_stop(uint8_t signal);  // for breakpoint-during-run [D29]
// No-op when no client connected (v2: per SW-7.2)

// Ctrl-C fast path (v2)
bool gdb_interrupt_requested(void);  // reads and clears atomic flag

typedef void (*gdb_log_func_t)(const char* msg);
void gdb_stub_set_log(gdb_log_func_t func);
```

#### 2.4 Compile-Time Gate [D28]

```cpp
#ifndef ENABLE_GDB_STUB
#define ENABLE_GDB_STUB 1
#endif

#if !ENABLE_GDB_STUB
// Inline no-op stubs — main.cpp compiles unchanged
static inline int gdb_stub_init(...) { return 0; }
static inline gdb_poll_result_t gdb_stub_poll() { return GDB_POLL_NONE; }
static inline bool gdb_interrupt_requested() { return false; }
// ... etc.
#endif
```

**Emscripten note** (per SW-9.2): Emscripten builds do not support POSIX sockets or `std::thread`. Emscripten builds must use `-DENABLE_GDB_STUB=0`.

### 3. Main Loop Integration

#### 3.1 Placement

`gdb_stub_poll()` inserted **before** the emulation tick block, at the top of the main loop body.

#### 3.2 State Variables [v2: refactored per D42]

```cpp
// main.cpp — file scope (moved from block scope)
static bool run_emulator = false;
static bool step_emulator = false;
static bool gdb_halted = false;      // NEW
```

Emulator ticks only when `run_emulator == true && gdb_halted == false`.

The `interrupt_requested` atomic is inside `gdb_stub.cpp`; accessed via `gdb_interrupt_requested()`.

#### 3.3 Poll Result Handling

```
switch (gdb_stub_poll()):
    GDB_POLL_HALTED:   run_emulator=false;  gdb_halted=true
    GDB_POLL_RESUMED:  gdb_halted=false;    run_emulator=true
    GDB_POLL_STEPPED:  gdb_halted=true;     run_emulator=false
    GDB_POLL_DETACHED: gdb_halted=false     (run_emulator unchanged)
    GDB_POLL_KILL:     gdb_halted=false;    run_emulator=true  [D12]
    GDB_POLL_NONE:     (no change)
```

#### 3.4 Tick Loop with Ctrl-C Fast Path [v2]

```cpp
if (run_emulator && !gdb_halted) {
    uint32_t timeout = SDL_GetTicks() + 13;
    while (!SDL_TICKS_PASSED(SDL_GetTicks(), timeout)) {
        emulator_step();
        steps++;

        // Check breakpoint (separate check for GDB, per SW-10.3)
        if (emulator_bp_hit()) {
            run_emulator = false;
            emulator_clear_bp_hit();
            if (gdb_stub_is_connected()) {
                gdb_halted = true;
                gdb_stub_notify_stop(5);
            }
            break;
        }

        // Check Ctrl-C from GDB (v2: atomic flag, instruction-level latency)
        if (gdb_interrupt_requested()) {
            run_emulator = false;
            gdb_halted = true;
            gdb_stub_notify_stop(2);
            break;
        }
    }
}
```

#### 3.5 ImGui Conflict Resolution [D30]

| Scenario | Behavior |
|----------|----------|
| GDB connected, user presses Run | `gdb_halted` gates tick block -> no effect |
| GDB connected, user presses Step | Same — gated |
| GDB sends `c` while user had stopped | GDB overrides: `run_emulator=true` |
| User presses Reset while GDB connected | Allowed; call `gdb_stub_notify_stop(5)` after |
| GDB disconnects | `gdb_halted=false`; emulator returns to prior state |

Run/Step buttons wrapped in `ImGui::BeginDisabled(gdb_halted)` [D30].
BP checkbox wrapped in `ImGui::BeginDisabled(gdb_stub_is_connected())` [D43].
Status text: "Halted (GDB)" when `gdb_halted && gdb_stub_is_connected()` [D31].

### 4. Build Integration

#### 4.1 Makefile Changes

```makefile
SOURCES += $(SRC_DIR)/gdb_stub.cpp

# Linux/macOS:
CXXFLAGS += -pthread
LDFLAGS  += -pthread
```

**(v2: changed from `-lpthread` to `-pthread` per SW-9.1)**. `-pthread` is both a compiler and linker flag; GCC requires it for correct `std::thread` behavior.

Existing pattern rule handles new `.cpp` files automatically.

#### 4.2 Platform [D36]

Linux/macOS only. Windows Winsock deferred (TODO comments in source). Emscripten must use `ENABLE_GDB_STUB=0`.

#### 4.3 Test Build Integration [v3: D46]

GDB stub tests integrate into the existing test harness infrastructure (see `docs/test-harness-spec-v2.md` and the `feature/test-harness` branch):

```makefile
# test/ directory already has doctest framework, test_main.cpp, test_helpers.h
# Add new test file:
# test/test_gdb_stub.cpp — GDB stub unit and protocol tests

# The gdb_stub.o production object is added to TEST_SRC_OBJS:
TEST_SRC_OBJS += $(BUILD_DIR)/gdb_stub.o
```

**Test approach**:
- **Protocol layer tests**: Test packet framing, checksum validation, NoAckMode, and command parsing directly using the internal parser functions. These are pure logic tests with no socket dependency.
- **Callback interface tests**: Use `EmulatorFixture` (from test harness) to test register accessors, memory access, step/continue via the callback struct.
- **Socket integration tests**: Deferred or run as a separate process. The doctest binary cannot easily spin up a TCP server within the test process. Consider: test the protocol parser in-process, test the full TCP stack via a separate GDB script or `gdb-multiarch` session.

**Existing test infrastructure reused** [D48]:
- `CpuFixture` — isolated CPU testing for step/halt alignment validation
- `EmulatorFixture` — full emulator state for callback testing
- `tty_inject_char()` — simulate TTY input for IRQ interaction tests with GDB
- `make_read_pins()` / `make_write_pins()` — TTY register access testing
- Link-time stubs (`test_stubs.cpp`) — satisfy ImGui symbols
- `< /dev/null` redirect — prevent stdin interference

### 5. Initialization and Lifecycle

```
main():
    emulator_init()
    gdb_stub_init(&config, &callbacks)  // after emulator is ready
    // main loop...
    gdb_stub_shutdown()                 // before SDL cleanup
```

- `gdb_stub_init()` stores callback pointers, starts TCP thread
- No callbacks called during init
- Safe to call `shutdown()` multiple times or after failed init
- Reconnection resets all state [D34]

### 6. Error Handling [D37]

| Error | Handling |
|-------|----------|
| `bind()` fails | `gdb_stub_init()` returns -1. Log. Emulator runs normally. |
| `accept()` fails | Log, retry after 1s sleep. Never crash. |
| `recv()` returns 0 | Clean disconnect -> enqueue DISCONNECT -> re-accept |
| `send()` fails | Treat as disconnect |
| TCP thread exception | Caught by try/catch. Log. Thread continues. |

### 7. ImGui Integration [D38]

Minimal changes:
- Status line in existing "Emulator Control" window: `GDB: Connected (port 3333)` / `GDB: Listening` / (nothing when disabled)
- Log callback wired to `gui_con_printmsg()` for connect/disconnect/error events
- No new ImGui windows
- BP checkbox disabled when GDB connected [D43]

---

## Part 4: Test Cases

### 4.1 Protocol Layer — Packet Framing

#### T01: Valid packet parsing
```
Input bytes: '$' 'g' '#' '6' '7'
Expected: packet payload = "g", checksum valid (0x67 = 'g'), ACK sent (+)
```

#### T02: Invalid checksum
```
Input bytes: '$' 'g' '#' '0' '0'
Expected: checksum mismatch (expected 0x67, got 0x00), NACK sent (-)
```

#### T03: Escaped bytes in packet
```
Input bytes: '$' 'm' '0' ',' '1' '}' 0x03 '#' XX XX
The '}' 0x03 sequence decodes to 0x03 XOR 0x20 = 0x23 = '#'
Expected: payload = "m0,1#" (the '#' is data, not delimiter)
Note: Checksum is computed over the raw byte stream ('m','0',',','1','}',0x03)
      NOT the decoded payload.
```

#### T04: Ctrl-C detection (0x03 outside packet)
```
State: IDLE
Input byte: 0x03
Expected: interrupt_requested atomic set to true, no packet parsed
```

#### T05: Ctrl-C ignored inside packet data
```
State: PACKET_DATA (mid-packet)
Input bytes: '$' 'g' 0x03 ... (malformed)
Expected: 0x03 appended to buffer (not treated as interrupt while in PACKET_DATA)
Note: This is actually protocol-ambiguous. GDB sends 0x03 outside packets.
      In practice, 0x03 inside $...# should not occur. If it does, treat as data.
```

#### T06: NoAckMode negotiation
```
1. Send: $QStartNoAckMode#b4
2. Receive: +$OK#9a
3. Verify: subsequent packets do NOT receive + or - ACK/NACK
4. Send: $g#67 (no ACK)
5. Receive: $<registers>#XX (no ACK prefix)
```

#### T07: NACK triggers retransmission
```
1. Stub sends: $T05thread:01;#XX
2. GDB replies: - (NACK)
3. Expected: stub retransmits $T05thread:01;#XX
```

#### T08: Maximum-size packet (64KB memory read)
```
Send: $m0,10000#XX
Receive: $<131072 hex chars>#XX
Verify: response is exactly 131072 hex chars (65536 bytes * 2)
Verify: checksum is correct over all 131072 chars
```

#### T09: Empty/unknown command
```
Send: $ZZZZ#XX
Receive: $#00 (empty response — unknown command)
```

#### T10: Partial packet followed by valid packet
```
Input bytes: '$' 'g' (no # — incomplete, more bytes arrive)
Then: '#' '6' '7'
Expected: valid packet "g" parsed after all bytes arrive
```

### 4.2 Register Access

#### T11: Read all registers (`g`)
```
Setup: A=0x42, X=0x10, Y=0xFF, SP=0xFD, PC=0xD000, P=0x24
Send: $g#67
Expected: $421000fffd00d024#XX
Verify: 14 hex chars total
Verify: PC is little-endian (0xD000 -> "00d0")
```

#### T12: Read single register (`p`)
```
Send: $p0#XX   -> A register
Expected: $42#XX (2 hex chars)

Send: $p4#XX   -> PC register
Expected: $00d0#XX (4 hex chars, little-endian)
```

#### T13: Write single register (`P`)
```
Send: $P0=ff#XX   -> write A=0xFF
Expected: $OK#XX
Verify: callback reports A=0xFF after write
```

#### T14: Write PC register (`P4`)
```
Send: $P4=00e0#XX   -> write PC=0xE000 (little-endian: 00e0)
Expected: $OK#XX
Verify: PC reads back as 0xE000
Verify: next step executes instruction at 0xE000 (observable behavior)
Verify: IRQ/NMI pin state preserved across PC write (v2: HW-13)
```

#### T15: Write all registers (`G`)
```
Send: $G001122330040a5#XX
Expected: $OK#XX
Verify: A=0x00, X=0x11, Y=0x22, SP=0x33, PC=0x4000 (LE: 0040), P=0xA5
```

#### T16: Invalid register number
```
Send: $p6#XX   -> register 6 doesn't exist
Expected: $E02#XX (bad register number)
```

#### T17: Flags register bit verification
```
Setup: P = 0xA5 = 10100101 -> N=1 V=0 -=1 B=0 D=0 I=1 Z=0 C=1
Send: $p5#XX
Expected: $a5#XX
Verify: each bit maps correctly to NV-BDIZC
```

#### T18: `G` packet wrong length [v2: SW-6.3]
```
Send: $G0011223300#XX     (10 hex chars, should be 14)
Expected: $E03#XX (parse error — wrong length)

Send: $G001122330040a5ff#XX  (16 hex chars, too long)
Expected: $E03#XX
```

### 4.3 Memory Access

#### T19: Read single byte
```
Setup: mem[0x0200] = 0xAB
Send: $m200,1#XX
Expected: $ab#XX
```

#### T20: Read range
```
Setup: mem[0x0200..0x0203] = {0xDE, 0xAD, 0xBE, 0xEF}
Send: $m200,4#XX
Expected: $deadbeef#XX
```

#### T21: Write single byte
```
Send: $M200,1:42#XX
Expected: $OK#XX
Verify: mem[0x0200] = 0x42
```

#### T22: Write range
```
Send: $M200,4:deadbeef#XX
Expected: $OK#XX
Verify: mem[0x0200..0x0203] = {0xDE, 0xAD, 0xBE, 0xEF}
```

#### T23: Read address boundaries
```
Send: $m0,1#XX          -> address 0x0000
Expected: $XX#XX (first byte of memory)

Send: $mffff,1#XX       -> address 0xFFFF
Expected: $XX#XX (last byte of memory)
```

#### T24: Read wrapping past end (error case)
```
Send: $mffff,2#XX       -> would read 0xFFFF and 0x10000
Expected: $E01#XX (address out of range)
```

#### T25: Write to ROM area ($D000+)
```
Setup: mem[0xD000] = 0x4C (original ROM byte)
Send: $Md000,1:EA#XX    -> write NOP over ROM
Expected: $OK#XX
Verify: mem[0xD000] = 0xEA
Note: ROM is writable (no protection in emulator) [D19]
```

#### T26: Read device-mapped region (no side effects)
```
Setup: TTY input queue has data (via tty_inject_char)
Send: $mc103,1#XX       -> TTY data register
Expected: returns mem[0xC103] (raw RAM), NOT pop from TTY queue [D17]
Verify: TTY queue length unchanged after read (via tty_buff_count)
Note (v3): if bus-decode read were used instead, empty queue would trigger BUG-1 UB
```

#### T27: Full 64KB memory dump
```
Send: $m0,10000#XX
Expected: 131072 hex chars response
Verify: checksum correct
Verify: response matches entire mem[] array
```

#### T28: Non-hex characters in memory command [v2: SW-5.1]
```
Send: $mZZZZ,1#XX
Expected: $E03#XX (parse error — non-hex characters)

Send: $M200,1:GG#XX
Expected: $E03#XX
```

#### T29: Address overflow [v2: SW-5.3]
```
Send: $m10000,1#XX      -> address > 0xFFFF
Expected: $E01#XX (bad address)
```

#### T26a: Read TTY phantom address via GDB [v3]
```
Setup: no TTY data in buffer
Send: $mc108,1#XX       -> TTY register 8 (phantom, within 0xC100-0xC10F range)
Expected: returns mem[0xC108] (raw RAM value, likely 0x00)
Note: address is within TTY bus decode range (mask 0xFFF0) but register 8
      hits the default case in tty_decode(), returning 0x00. GDB bypasses
      bus decode so this is safe regardless.
```

### 4.4 Breakpoints

#### T30: Set and clear breakpoint
```
Send: $Z0,d000,1#XX    -> set bp at 0xD000
Expected: $OK#XX
Verify: bp_mask[0xD000] == true, bp_enable == true

Send: $z0,d000,1#XX    -> clear bp at 0xD000
Expected: $OK#XX
Verify: bp_mask[0xD000] == false
```

#### T31: Set multiple breakpoints
```
Send: $Z0,d000,1#XX    -> bp at 0xD000
Send: $Z0,d010,1#XX    -> bp at 0xD010
Send: $Z0,d020,1#XX    -> bp at 0xD020
Expected: all three set, bp_enable == true
```

#### T32: Z1 (hardware bp) maps to same mechanism [D10]
```
Send: $Z1,d000,1#XX
Expected: $OK#XX
Verify: bp_mask[0xD000] == true (same as Z0)
```

#### T33: Unsupported breakpoint types
```
Send: $Z2,d000,1#XX    -> write watchpoint
Expected: $#00 (empty — unsupported)

Send: $Z3,d000,1#XX    -> read watchpoint
Expected: $#00

Send: $Z4,d000,1#XX    -> access watchpoint
Expected: $#00
```

#### T34: Breakpoint fires only on SYNC (instruction fetch) [D32]
```
Setup: bp_mask[0x0200] = true, mem[0x0200] = 0x42
Instruction at 0xD000: LDA $0200 (reads data from 0x0200)
Run from 0xD000.
Expected: breakpoint does NOT fire (0x0200 accessed as data, not instruction fetch)
Verify: bp_hit remains false during the LDA data read cycle
Note (v3): requires D32 SYNC fix. Current emulator.cpp:86 WILL falsely trigger.
```

#### T35: Breakpoint fires on instruction at BP address
```
Setup: bp_mask[0xD000] = true, instruction at 0xD000: NOP
Run from 0xCFFE (instruction before 0xD000).
Expected: breakpoint fires when PC reaches 0xD000 (SYNC cycle)
Verify: stop reply T05thread:01; sent
```

#### T36: Breakpoint during step [v2: SW-6.5]
```
Setup: bp_mask[0xD001] = true, PC at 0xD000, mem[0xD000] = 0xEA (NOP)
Send: $s#XX
Expected: $T05thread:01;#XX (SIGTRAP — normal step completion)
Verify: PC at 0xD001
Note: breakpoint at step destination does not produce a different signal.
      Step always reports SIGTRAP on normal completion.
```

### 4.5 Execution Control — Stepping

#### T37: Step NOP (2 cycles)
```
Setup: PC at 0xD000, mem[0xD000] = 0xEA (NOP)
Send: $s#XX
Expected: $T05thread:01;#XX
Verify: PC now at 0xD001
Verify: exactly 2 calls to emulator_step()
Verify: pins & M6502_SYNC is true after step
```

#### T38: Step LDA absolute (4 cycles)
```
Setup: PC at 0xD000, mem = [0xAD, 0x00, 0x02, ...] (LDA $0200)
       mem[0x0200] = 0x42
Send: $s#XX
Expected: $T05thread:01;#XX
Verify: PC at 0xD003, A = 0x42
Verify: exactly 4 calls to emulator_step()
```

#### T39: Step BRK (7 cycles)
```
Setup: PC at 0xD000, mem[0xD000] = 0x00 (BRK)
       IRQ vector at 0xFFFE/0xFFFF = 0xE000
Send: $s#XX
Expected: $T05thread:01;#XX
Verify: PC at 0xE000 (IRQ handler)
Verify: stack contains return address and flags
```

#### T40: Step JAM instruction (guard fires)
```
Setup: PC at 0xD000, mem[0xD000] = 0x02 (JAM/KIL)
Send: $s#XX
Expected: $T04thread:01;#XX (SIGILL — illegal instruction) [D21]
Verify: guard counter hit limit (16 cycles)
```

#### T41: Step with address parameter
```
Setup: PC at 0xD000
Send: $sd010#XX         -> step, but set PC to 0xD010 first
Expected: $T05thread:01;#XX
Verify: instruction at 0xD010 was executed, not 0xD000
```

#### T42: Multiple sequential steps
```
Setup: PC at 0xD000, three NOPs at 0xD000-0xD002
Send: $s#XX -> verify PC=0xD001
Send: $s#XX -> verify PC=0xD002
Send: $s#XX -> verify PC=0xD003
Verify: each step returns T05thread:01;, each advances PC by 1
```

### 4.6 Execution Control — Continue

#### T43: Continue then breakpoint hit
```
Setup: PC at 0xD000, bp at 0xD010
       Instructions 0xD000-0xD00F are NOPs, 0xD010 is NOP
Send: $c#XX
Expected: no immediate response
... emulator runs through NOPs ...
... hits bp at 0xD010 ...
Expected: $T05thread:01;#XX (async stop reply)
Verify: PC = 0xD010
```

#### T44: Continue with address parameter
```
Setup: PC at 0xD000, bp at 0xD020
Send: $cd010#XX         -> continue from 0xD010
Expected: emulator runs from 0xD010, hits bp at 0xD020
Receive: $T05thread:01;#XX
Verify: PC = 0xD020
```

#### T45: Continue then Ctrl-C interrupt
```
Setup: PC at 0xD000, infinite loop at 0xD000: JMP $D000
Send: $c#XX
... emulator running (infinite loop) ...
Send: 0x03 (Ctrl-C)
Expected: $T02thread:01;#XX (SIGINT)
Verify: emulator halted
Verify: PC somewhere in the loop (0xD000-ish)
```

#### T46: Continue with no breakpoints (must use Ctrl-C to stop)
```
Setup: PC at 0xD000, no breakpoints, infinite loop
Send: $c#XX
... runs forever ...
Send: 0x03
Expected: $T02thread:01;#XX
```

### 4.7 Connection and Lifecycle

#### T47: Connect halts emulator [D23, v3: D49]
```
Setup: emulator running freely
Action: GDB connects via TCP
Expected: emulator halts
Expected: CPU at instruction boundary (pins & M6502_SYNC)
Verify: gdb_stub_poll() returns GDB_POLL_HALTED
Note (v3): if emulator was just m6502_init()'d, SYNC is already set.
           Alignment code must handle this (check-first pattern per D49).
```

#### T48: Disconnect resumes emulator
```
Setup: GDB connected, emulator halted
Action: GDB disconnects (TCP close)
Expected: gdb_stub_poll() returns GDB_POLL_DETACHED
Verify: gdb_halted = false
Verify: GDB-set breakpoints cleared [D44]
```

#### T49: Reconnect after disconnect
```
Setup: GDB was connected, then disconnected. Emulator running.
Action: new GDB connects
Expected: emulator halts again (fresh state)
Expected: no carryover from previous session [D34]
```

#### T50: Detach command
```
Send: $D#XX
Expected: $OK#XX
Verify: emulator resumes, GDB disconnected
Verify: GDB-set breakpoints cleared [D44]
Verify: next gdb_stub_poll() returns GDB_POLL_DETACHED
```

#### T51: Kill command [D12]
```
Send: $k#XX
Expected: no response (GDB doesn't expect one)
Verify: emulator resumes (not shut down)
Verify: gdb_stub_poll() returns GDB_POLL_KILL
```

#### T52: No-client overhead
```
Setup: gdb_stub_init() called, no GDB client connected
Measure: time for gdb_stub_poll()
Expected: returns GDB_POLL_NONE in < 1 microsecond
Verify: single atomic load path (code review) [D33]
```

#### T53: Init failure — port in use
```
Setup: another process bound to port 3333
Action: gdb_stub_init(port=3333)
Expected: returns -1
Verify: emulator runs normally without GDB
Verify: gdb_stub_poll() returns GDB_POLL_NONE (safe)
```

#### T54: Shutdown while connected
```
Setup: GDB connected, emulator halted
Action: gdb_stub_shutdown()
Expected: TCP thread joins cleanly
Expected: client socket closed (GDB sees disconnect)
Verify: no crash, no resource leak
```

### 4.8 Query Commands

#### T55: qSupported handshake
```
Send: $qSupported:multiprocess+;swbreak+;hwbreak+#XX
Expected: $PacketSize=20000;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+#XX
```

#### T56: Target XML transfer
```
Send: $qXfer:features:read:target.xml:0,fff#XX
Expected: response starts with 'l' (last chunk — XML < 4KB)
Expected: XML does NOT contain <architecture> tag (v2: omitted)
Expected: XML contains <feature name="org.n8machine.cpu"> (v2: renamed)
Expected: XML contains 6 register definitions (a, x, y, sp, pc, flags)
```

#### T57: Target XML chunked transfer
```
Send: $qXfer:features:read:target.xml:0,10#XX   -> only 16 bytes
Expected: response starts with 'm' (more data available)
Verify: response body is exactly 16 bytes of XML

Send: $qXfer:features:read:target.xml:10,fff#XX -> rest
Expected: response starts with 'l' (last chunk)
```

#### T58: Memory map XML transfer [v2, v3: updated content]
```
Send: $qXfer:memory-map:read::0,fff#XX
Expected: response starts with 'l' (last chunk)
Expected: XML contains ram and rom memory regions
Expected: ROM region starts at 0xD000
Expected (v3): TTY region is $C100 length $0010 (16 bytes)
Expected (v3): Frame buffer region is $C000 length $0100 (256 bytes)
```

#### T59: Thread info (single-threaded target)
```
Send: $qfThreadInfo#XX
Expected: $m01#XX

Send: $qsThreadInfo#XX
Expected: $l#XX
```

#### T60: qAttached
```
Send: $qAttached#XX
Expected: $1#XX (attached to existing process)
```

#### T61: Thread selection (any thread ID accepted)
```
Send: $Hg0#XX
Expected: $OK#XX

Send: $Hc-1#XX
Expected: $OK#XX
```

#### T62: qRcmd reset [v2, v3: updated implementation]
```
Send: $qRcmd,7265736574#xx  ("reset" hex-encoded)
Expected: $O52657365740a#XX  (hex-encoded "Reset\n")
Followed by: $OK#XX
Verify (v3): M6502_RES pin asserted (NOT emulator_reset() called) [D47]
Verify (v3): tty_reset() called
Verify (v3): ROM still intact in mem[$D000+] (not reloaded from file)
```

#### T63: qRcmd unknown command [v2]
```
Send: $qRcmd,666f6f#XX      ("foo" hex-encoded)
Expected: hex-encoded "Unknown monitor command\n" output, then $OK#XX
```

### 4.9 Stop Reason Persistence [v2: SW-1.8, SW-6.1]

#### T64: `?` returns SIGTRAP on fresh connect
```
Setup: emulator running normally
Action: GDB connects
Send: $?#XX
Expected: $T05thread:01;#XX (SIGTRAP)
```

#### T65: `?` returns SIGILL after JAM [v2: SW-6.1]
```
Setup: PC at 0xD000, mem[0xD000] = 0x02 (JAM)
Send: $s#XX
Expected: $T04thread:01;#XX
Send: $?#XX
Expected: $T04thread:01;#XX (persisted SIGILL)
```

### 4.10 Integration Tests

#### T66: Full GDB handshake -> inspect -> step -> continue -> breakpoint
```
1. Connect
2. $qSupported -> verify capabilities include qXfer:memory-map:read+
3. $QStartNoAckMode -> $OK
4. $? -> $T05thread:01; (halted)
5. $g -> read all registers
6. $m d000,10 -> read 16 bytes of ROM
7. $Z0,d010,1 -> set breakpoint
8. $s -> step one instruction -> $T05thread:01;
9. $g -> verify PC advanced
10. $c -> continue -> ... -> $T05thread:01; (breakpoint hit)
11. $g -> verify PC = 0xD010
12. $z0,d010,1 -> clear breakpoint
13. $D -> detach -> $OK
```

#### T67: Memory write -> verify via read
```
1. Connect
2. $Md000,4:deadbeef#XX -> write 4 bytes
3. $md000,4#XX -> read back
4. Verify response is "deadbeef"
```

#### T68: Register write -> step -> verify effect
```
Setup: NOP at 0xE000
1. $P4=00e0#XX -> set PC to 0xE000
2. $s#XX -> step one NOP
3. $p4#XX -> read PC
4. Verify: PC = 0xE001 (little-endian: "01e0")
```

#### T69: Breakpoint at reset vector
```
1. Set bp at reset vector target
2. Send reset via qRcmd (v3: M6502_RES pin, D47)
3. Continue
4. Verify: breaks at reset vector entry point
```

#### T70: Step through I/O instruction
```
Setup: instruction at 0xD000: STA $C000 (write to frame buffer)
       A = 0x42
1. Step
2. Verify: frame_buffer[0] = 0x42 (device ticked during step) [D39]
3. Verify: PC advanced past the STA
```

#### T71: Memory write then execute [v2: SW-6.6]
```
Setup: PC at 0xD000
1. $Md000,1:ea#XX -> write NOP at current PC
2. $s#XX -> step
3. Verify: PC = 0xD001 (NOP executed)
4. Verify: $T05thread:01;#XX
```

#### T70a: Step through TTY write instruction [v3]
```
Setup: instruction at 0xD000: LDA #$48; STA $C101 (write 'H' to TTY)
       A pre-loaded via register write
1. Step through LDA
2. Step through STA
3. Verify: putchar('H') called (via stdout capture or accept side effect)
4. Verify: device ticked during step [D39]
Note: uses test harness knowledge that tty_decode write to reg 1 calls putchar
```

#### T69a: Reset preserves breakpoints [v3]
```
Setup: GDB connected, bp at 0xD000
1. Send qRcmd reset [D47]
2. Verify: bp_mask[0xD000] still true
3. Send $c#XX (continue)
4. Verify: breakpoint fires at 0xD000 after reset
Note: D47 reset does not clear breakpoints (only D44 disconnect does)
```

### 4.11 Edge Cases and Error Handling

#### T72: Malformed memory read (missing comma)
```
Send: $m200#XX (no length)
Expected: $E03#XX (parse error)
```

#### T73: Malformed register write
```
Send: $P#XX (no register number or value)
Expected: $E03#XX
```

#### T74: Zero-length memory read
```
Send: $m200,0#XX
Expected: $#XX (empty response — zero bytes)
```

#### T75: Rapid command sequence
```
Send: $g#XX -> receive response
Send: $g#XX -> receive response
Send: $g#XX -> receive response
(3 rapid reads)
Expected: all three return identical register state (CPU is halted)
```

#### T76: Breakpoint at address 0x0000
```
Send: $Z0,0,1#XX
Expected: $OK#XX
Verify: bp_mask[0] == true
```

#### T77: Breakpoint at address 0xFFFF
```
Send: $Z0,ffff,1#XX
Expected: $OK#XX
Verify: bp_mask[0xFFFF] == true
```

#### T78: Double-set breakpoint (idempotent)
```
Send: $Z0,d000,1#XX -> $OK
Send: $Z0,d000,1#XX -> $OK (no error, already set)
```

#### T79: Clear non-existent breakpoint (idempotent)
```
Send: $z0,d000,1#XX -> $OK (no error, nothing to clear)
```

#### T80: GDB disconnect mid-continue
```
1. $c#XX -> emulator running
2. GDB closes TCP connection (no Ctrl-C, no detach)
3. Expected: TCP thread detects disconnect
4. Expected: DISCONNECT enqueued (v2)
5. Expected: emulator resumes, gdb_halted cleared
6. Expected: stub returns to accept() state
```

#### T81: Socket error during send
```
Setup: GDB connected
Simulate: network error on next send()
Expected: treated as disconnect
Expected: emulator resumes, stub re-accepts
```

#### T82: Concurrent ImGui Step while GDB halted [D30]
```
Setup: GDB connected, gdb_halted = true
Action: user clicks "Step" button in ImGui
Expected: no effect (gated by gdb_halted)
Verify: emulator_step() NOT called
```

#### T83: Disconnect while GDB halted (not running) [v2: SW-2.4]
```
Setup: GDB connected, emulator halted (gdb_halted=true), NOT in continue
Action: GDB closes TCP connection
Expected: DISCONNECT command enqueued
Expected: gdb_stub_poll() returns GDB_POLL_DETACHED
Verify: gdb_halted cleared to false
Verify: emulator can be controlled via ImGui again
```

#### T84: Rapid connect/disconnect [v2: SW-6.8]
```
1. GDB connects -> halts emulator
2. GDB immediately disconnects
3. GDB connects again
4. Verify: clean state, no crash, no stale data
5. $? -> $T05thread:01; (fresh halt)
```

#### T85: `bp_enable` forced true when GDB sets breakpoint [v2: D43]
```
Setup: ImGui BP checkbox unchecked (bp_enable = false via emulator_enablebp)
Action: GDB connects
Send: $Z0,d000,1#XX
Expected: $OK#XX
Verify: bp_enable == true (forced by GDB callback)
Verify: ImGui BP checkbox appears disabled (grayed out)
```

### 4.12 Hardware-Relevant Test Cases [v2: from HW critique]

#### T-HW1: NMI during single step
```
Setup: PC at 0xD000, NMI pin asserted, mem[0xD000] = 0xEA (NOP)
       NMI vector at 0xFFFA/0xFFFB = 0xE000
Send: $s#XX
Expected: $T05thread:01;#XX
Verify: PC at NMI handler entry (0xE000)
Verify: stack contains return address from 0xD000 area
Note: NMI has priority; step completes at handler entry.
```

#### T-HW2: IRQ arrives while halted, fires on resume
```
Setup: CPU halted at 0xD000 (I flag clear, IRQ enabled)
Action: Set IRQ condition (via tty_inject_char to populate TTY buffer) [v3: D48]
Send: $c#XX
Expected: CPU vectors to IRQ handler on resume
Verify: PC at IRQ handler, not 0xD000
Note: In emulator, IRQ is frozen while halted (D40). IRQ state is
      evaluated on first tick after resume.
Note (v3): tty_inject_char() provides deterministic IRQ triggering for tests.
```

#### T-HW3: Reset during GDB session [v3: updated per D47]
```
Setup: GDB connected, breakpoints set at 0xD000
Action: Reset via qRcmd "reset" (sets M6502_RES pin, calls tty_reset)
Send: $c#XX
Expected: CPU executes from reset vector
Verify: breakpoints survive the reset
Verify: bp at 0xD000 fires if execution reaches it
Verify (v3): ROM in mem[$D000+] was NOT reloaded from file
```

#### T-HW4: Breakpoint in IRQ handler while running
```
Setup: bp at IRQ handler entry (read from 0xFFFE/0xFFFF)
       IRQ will fire during normal execution (via tty_inject_char) [v3: D48]
Send: $c#XX
... emulator runs, IRQ fires, vectors to handler ...
Expected: $T05thread:01;#XX
Verify: PC at IRQ handler entry
Verify: stack contains correct return address
```

#### T-HW5: IRQ state preserved across PC write [v2: HW-13]
```
Setup: IRQ pin currently asserted (pins & M6502_IRQ != 0)
Action: Write PC via $P4=00e0#XX
Verify: After PC write, (pins & M6502_IRQ) still asserted
Verify: NMI pin state also preserved
```

### 4.13 Test Harness Cross-Reference Tests [v3: new]

These test cases specifically validate behaviors discovered or clarified by the test harness implementation.

#### T-TH1: GDB reset does not reload ROM [v3: D47]
```
Setup: GDB connected. Patch mem[0xD000] = 0xEA (NOP) via GDB write.
       Original ROM file has different byte at 0xD000.
Action: qRcmd reset
Verify: mem[0xD000] still == 0xEA (patch preserved, ROM not reloaded)
Note: This validates D47 — the reset callback uses M6502_RES pin,
      not emulator_reset() which would reload ROM from file.
```

#### T-TH2: GDB read of TTY register returns mem[], not device state [v3: D17 + BUG-1]
```
Setup: tty_inject_char('A'). Buffer has 1 char.
Action: GDB reads mem[0xC103] (direct mem[] access, not bus decode)
Verify: returns whatever raw value is in mem[0xC103] (not 0x41)
Verify: tty_buff_count() still == 1 (queue not popped)
Note: validates that D17 (bypass bus decode) prevents BUG-1 UB and
      preserves TTY queue state.
```

#### T-TH3: IRQ register ephemeral during step [v3: TH-IRQ]
```
Setup: GDB connected. tty_inject_char('A') to populate buffer.
Action: Step one instruction (CLI or NOP with I flag clear).
        Immediately read mem[0x00FF] via GDB.
Verify: mem[0x00FF] may be 0x00 or 0x02 depending on timing within
        emulator_step(). The IRQ_CLR/tty_tick/assert pipeline runs
        within each step call.
Note: documents that mem[0x00FF] is not a reliable snapshot of IRQ
      state via direct mem[] read. GDB should infer IRQ state from
      pin inspection or register state, not mem[0x00FF].
```

#### T-TH4: SYNC state after m6502_init [v3: D49]
```
Setup: Fresh emulator_init(). GDB connects.
Verify: pins & M6502_SYNC is true (SYNC already set post-init).
Verify: alignment code in on_gdb_connect() recognizes this and skips
        unnecessary ticking.
Verify: GDB can immediately read registers and memory.
```

---

## Implementation Files Summary

### New Files
- `src/gdb_stub.h` — public API, callback struct, config, lifecycle, poll
- `src/gdb_stub.cpp` — TCP thread, queues, packet framing, RSP protocol

### Modified Files
- `src/emulator.h` — add register accessor functions (read/write A,X,Y,S,P,PC), add `emulator_bp_hit()`, `emulator_clear_bp_hit()`, `emulator_bp_enabled()`
- `src/emulator.cpp` — implement accessors, add SYNC check to bp detection [D32], add `clear_breakpoint()`, separate `bp_hit` check/clear, add `last_stop_signal`
- `src/main.cpp` — move `run_emulator`/`step_emulator` to file scope [D42], remove local `bp_enable` shadow, wire callbacks, call init/poll/shutdown, add `gdb_halted`, gate tick block, add Ctrl-C fast path in tick loop, update ImGui status, disable BP checkbox when GDB connected
- `Makefile` — add `gdb_stub.cpp` to SOURCES, add `-pthread`

### Already Modified (by test harness)
- `src/emu_tty.cpp` — `tty_inject_char()` and `tty_buff_count()` added (feature/test-harness branch)
- `src/emu_tty.h` — declarations for `tty_inject_char()` and `tty_buff_count()`

### Estimated Size
- `gdb_stub.h`: ~100 lines (v2: slightly larger with monitor_cmd, interrupt_requested)
- `gdb_stub.cpp`: ~550-650 lines (v2: memory map XML, qRcmd, validation)
- `emulator.h` changes: ~25 lines
- `emulator.cpp` changes: ~50 lines
- `main.cpp` changes: ~60 lines
- **Total new/modified: ~800-900 lines**

---

## Future Work

| Item | Source | Notes |
|------|--------|-------|
| Watchpoints (Z2/Z3/Z4) | HW-7 | Write/read/access watchpoints. Response: `T05watch:<addr>;`. Useful for I/O region debugging. Address comparator + R/W qualification. |
| Minimal `vCont` support | SW-1.9 | `vCont;c;s;t` advertisement + `vCont;c`/`vCont;s` as aliases. Revisit if gdb-multiarch compatibility issues arise. |
| Configurable step guard | HW-10 | Add to `gdb_stub_config_t`. Default 16 for emulator, 32+ for hardware. |
| Windows Winsock support | D36 | Replace POSIX socket calls with Winsock equivalents. |
| `stdin` redirect during GDB | SW-10.2 | `tty_tick()` calls `select()` on stdin during step. May consume keystrokes. |
| BUG-1 fix: tty_decode empty queue guard | TH-BUG-1 | Add `if (tty_buff.size() == 0) { data_bus = 0x00; break; }` before `front()`/`pop()` in case 0x03 of `tty_decode()`. Low risk but blocks firmware test FW-* cases that exercise empty-buffer reads. |
| D32 SYNC fix implementation | TH-BP | Add `&& (pins & M6502_SYNC)` to breakpoint check at emulator.cpp:86. Prerequisite for GDB breakpoint correctness. |

---

## Appendix A: Hardware Port Notes

This appendix collects concerns raised by the Hardware critique that are relevant to a future port of this GDB stub to physical 6502 hardware (e.g., over UART). These do NOT affect the emulator implementation but are documented here for reference.

### A.1 RDY Pin for Halt (HW-1)

**Emulator approach**: Halt by setting `run_emulator = false` (stop calling `m6502_tick()`).

**Hardware reality**: The real 6502 has a RDY pin (pin 28) that halts the CPU during read cycles. The m6502.h emulator faithfully implements RDY at line 722. On NMOS 6502, RDY only halts on read cycles; on WDC 65C02, it halts on both read and write.

**Port implications**: The `halt()` callback implementation must change to assert RDY and continue ticking (to keep the IRQ pipeline active — see A.3). A hardware port should test halt during write cycles on NMOS targets.

### A.2 Memory Access Model (HW-2)

**Emulator approach**: Direct `mem[]` access bypasses bus decode [D17]. No side effects.

**Hardware reality**: There is no "bypass bus decode" path on physical hardware. Reading TTY_IN_DATA ($C103) WILL pop from the FIFO. Writing to ROM is a no-op.

**Port implications**:
- Add `read_mem_mode` field to callback struct (`MEM_BYPASS` vs `MEM_BUS`)
- `qXfer:memory-map:read` (implemented in v2, refined in v3) helps GDB understand which regions are I/O
- Consider `read_mem_safe(addr)` that returns error for I/O regions
- Test case T26 (read device-mapped region without side effects) is emulator-only

**(v3 note)**: BUG-1 (empty queue UB in `tty_decode()`) means even the emulator's bus-decode path is unsafe for $C103 reads when the buffer is empty. A hardware port must handle the empty-buffer case at the FIFO level.

### A.3 IRQ Pipeline During Halt (HW-3)

**Emulator approach**: Not calling `m6502_tick()` freezes the `irq_pip` and `nmi_pip` shift registers [D40].

**Hardware reality**: IRQ and NMI are continuous electrical signals. When using RDY-based halting, `irq_pip <<= 1` continues to shift (m6502.h line 725 runs inside the RDY block).

**Port implications**: If the hardware port uses RDY-based halting (A.1), IRQ pipeline behavior matches reality. Document that halt/resume IRQ timing differs between emulator and hardware.

### A.4 PC Write Mechanism (HW-4)

**Emulator approach**: Direct pin/register manipulation (prefetch sequence) [D16].

**Hardware reality**: CPU owns the address bus. To redirect execution requires bus stuffing (force JMP opcode + address bytes onto data bus).

**Port implications**: The callback architecture (D35) correctly isolates this. The `write_reg16()` callback implementation will differ completely on hardware.

### A.5 Transport Layer (HW-9)

**Emulator approach**: TCP sockets [D1].

**Hardware reality**: UART with interrupt-driven receive. No connection concept.

**Port implications**: Part 1 Sections 1-2 (TCP socket, threading) are replaced entirely. Part 1 Sections 3-4 (packet framing, command parsing) port directly. The transport/protocol separation note at the top of Part 1 documents this.

### A.6 Bus Decode Ordering (HW-12)

In `emulator.cpp`, writes go to both `mem[]` and the device. Reads first load from `mem[]` then get overwritten by device data. This means `mem[0xC103]` contains the last CPU-written value, not current device state. On hardware, there is no `mem[]` backing store for device addresses.

**(v3 note)**: The test harness (T68: "Write hits both mem and device") validated this dual-write behavior. For the GDB stub, this means `mem[]` reads of device-mapped addresses return stale write data, not current device state. This is documented as a known limitation in Section 3 of Part 2.

### A.7 `cur_instruction` Tracking (HW-5 caveat)

The existing `cur_instruction` tracking (emulator.cpp lines 81-84) uses address comparison (`addr == m6502_pc(&cpu)`), not SYNC. This should also be gated on SYNC for consistency with D32, though it is not blocking for GDB stub functionality.

**(v3 note)**: The test harness (T96b) validates that `emulator_getci()` returns the correct address. The address-matching approach works in practice because `m6502_pc()` typically matches the bus address only during opcode fetch cycles. However, for robustness, a SYNC gate would be more correct.

---

## Appendix B: Test Harness Cross-Reference [v3: new]

This appendix documents the relationship between the GDB stub spec and the test harness implementation (see `docs/test-harness-spec-v2.md`).

### B.1 Bugs Found by Test Harness

| Bug ID | Location | Impact on GDB Stub |
|--------|----------|-------------------|
| BUG-1 | `emu_tty.cpp:87-88` — `front()`/`pop()` on empty queue | Reinforces D17 (bypass bus decode). GDB must never trigger a bus-decode read at $C103 with empty buffer. |
| BUG-2 | `firmware/tty.s:47` — `SBC` without `SEC` | Affects firmware tests (FW-10a) only. No direct GDB stub impact, but GDB users debugging this firmware code will observe the bug. |
| BUG-3 | `emu_labels.cpp:17` — `char*` vs `const char*` | No GDB stub impact. Labels are read-only from GDB perspective. |

### B.2 Production Code Changes Available to GDB

| Change | File | Usage in GDB Stub |
|--------|------|-------------------|
| `tty_inject_char(uint8_t c)` | `emu_tty.cpp` | GDB stub tests: deterministic TTY input for IRQ testing (T-HW2, T-HW4) |
| `tty_buff_count()` | `emu_tty.cpp` | GDB stub tests: verify TTY queue state not corrupted by GDB reads (T-TH2) |

### B.3 Validated Assumptions

The test harness validated several assumptions that the GDB stub spec relies on:

| Assumption | Test Harness Validation | GDB Spec Reference |
|------------|------------------------|-------------------|
| `M6502_SYNC` is set after `m6502_init()` | CpuFixture `boot()` do-while fix | D23, D49 |
| `emulator_check_break()` clears `bp_hit` | T97, T98 | D29, SW-10.3 |
| `emulator_enablebp()` controls breakpoint checking | T97 (enable), T98 (disable) | D43 |
| Frame buffer write via bus decode works correctly | T64, T99 | T70 (GDB step through I/O) |
| IRQ pipeline: `IRQ_CLR()` → `tty_tick()` → pin assert | T101, T101a | T-HW2, T-HW4 |
| TTY phantom addresses (4-15) return 0x00 | T78a | Memory Map XML Section 6 |
| TTY bus decode mask is 0xFFF0 (16 addresses) | T67a | Memory Map XML Section 6 |
| Direct `mem[]` access bypasses devices | T62, T63 (vs T64, T65) | D17 |
| `emulator_reset()` requires files on disk | EmulatorFixture avoids it | D47 |

### B.4 Test Infrastructure Reuse

The GDB stub implementation should reuse these test harness components:

| Component | Location | Reuse Pattern |
|-----------|----------|---------------|
| doctest v2.4.11 | `test/doctest.h` | Framework for `test/test_gdb_stub.cpp` |
| `test_main.cpp` | `test/test_main.cpp` | Shared entry point (no changes needed) |
| `test_helpers.h` | `test/test_helpers.h` | CpuFixture, EmulatorFixture, pin helpers, externs |
| `test_stubs.cpp` | `test/test_stubs.cpp` | ImGui + gui_console link-time stubs |
| `make test` target | `Makefile` | Add `gdb_stub.o` to `TEST_SRC_OBJS`, add `test_gdb_stub.cpp` |
| `< /dev/null` redirect | `Makefile` test target | Prevents stdin interference |
