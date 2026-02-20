# GDB Remote Serial Protocol Stub — Implementation Spec for N8machine

## Reconciled Design Decisions

All decisions were evaluated across 3 independent agents (A: Protocol, B: Emulator Integration, C: Architecture). Decisions marked with agreement count.

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
| D13 | Target XML with `6502` architecture, 6 registers | GDB can read/write registers; no native disassembly | A,B |
| D14 | Expose emulator state via accessor functions in `emulator.h` | Agent B wanted `extern`, Agent C wanted accessors. Accessors win — avoids 143KB m6502.h include leak, keeps CPU struct private. **Resolved: B+C agree on accessor approach.** | B,C |
| D15 | PC writes only valid at instruction boundary | GDB only writes registers when target is stopped; halt/step always land on boundaries | B |
| D16 | Use full prefetch sequence for GDB PC writes | Safe regardless of internal CPU state; `M6502_SYNC + SET_ADDR + SET_DATA + set_pc` | B |
| D17 | GDB memory reads bypass bus decode; direct `mem[]` access | Non-destructive reads; avoids TTY queue corruption | B,C |
| D18 | GDB memory writes are plain `mem[addr] = val`, no device overlay sync | Simple, predictable. Document limitation for device-mapped regions. | B |
| D19 | Allow GDB writes to ROM area | Useful for patching; no write protection in hardware model | B |
| D20 | Detect instruction boundaries via `pins & M6502_SYNC` | Documented mechanism in m6502.h (line 124-125); more reliable than address-matching | B |
| D21 | Report JAM as SIGILL (signal 4) instead of SIGTRAP | Distinguishes illegal instruction from normal step completion | B |
| D22 | Step guard counter = 16 cycles | Covers worst case: 7-cycle instruction + 7-cycle interrupt vectoring + margin | B |
| D23 | Advance to instruction boundary on initial GDB halt | Ensures clean state for all subsequent GDB operations | B,C |
| D24 | Two files: `gdb_stub.h` + `gdb_stub.cpp` | Matches codebase convention. Split to three if >800 lines. | C |
| D25 | C++ API, not `extern "C"` | No C consumers exist; codebase is C++ throughout | C |
| D26 | Raw function pointers in callback struct | Matches `m6502_desc_t` style; zero overhead; C++11 compatible | B,C |
| D27 | `gdb_stub_poll()` returns an enum | Main loop needs to know what happened; enum is simplest | C |
| D28 | Runtime `config.enabled` + compile-time `ENABLE_GDB_STUB` | Runtime for convenience, compile-time for zero footprint | C |
| D29 | `gdb_stub_notify_stop(signal)` for breakpoint-during-run | Main thread pushes stop replies to TCP thread via response queue | B,C |
| D30 | GDB has priority over ImGui when connected | Run/Step gated by `gdb_halted` flag; ImGui controls disabled | B,C |
| D31 | Three-state status: Running / Halted / Halted (GDB) | User needs to understand why controls are unresponsive | C |
| D32 | Breakpoint check requires M6502_SYNC pin (instruction fetch only) | GDB execution breakpoints should not trigger on data access. Correctness fix for both GDB and existing debugger. | B |
| D33 | `gdb_stub_poll()` early-return on no client (single atomic load) | Zero overhead when no GDB client connected | C |
| D34 | Reconnection resets all protocol state; new connect halts emulator | Clean semantics; no stale state confusion | A,C |
| D35 | `gdb_stub.cpp` has zero N8machine includes | All emulator access through callbacks; stub is reusable | C |
| D36 | Linux/macOS only initially; Windows Winsock deferred | Codebase is Linux-focused | C |
| D37 | Top-level try/catch in TCP thread | Thread must never crash; emulator unaffected by stub failure | C |
| D38 | Minimal ImGui: status text in existing window + console log | No new ImGui window; consistent with codebase UI style | C |
| D39 | Devices tick normally during GDB single-step | Preserves cycle accuracy; required for I/O instruction debugging | B |
| D40 | IRQ state frozen while halted, resumes on continue | Natural consequence of not calling m6502_tick() | B |
| D41 | TTY does not tick when halted | No emulated I/O when CPU stopped; correct behavior | B |

---

## Part 1: Protocol & Network Layer

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
2. Resume emulator execution (detach)
3. Return to `accept()` for new connection

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
- Enqueues parsed commands → command queue
- Dequeues responses ← response queue, frames and sends

**Main Thread** (existing SDL/ImGui loop):
- Polls command queue once per frame via `gdb_stub_poll()` [D33]
- Executes commands via callback interface
- Enqueues responses to response queue
- Controls emulator run/halt state

#### 2.2 Command Queue (Main Thread Inbox)

```cpp
struct GdbCommand {
    enum Type { QUERY, STEP, CONTINUE, READ_REG, WRITE_REG,
                READ_MEM, WRITE_MEM, SET_BP, CLEAR_BP,
                DETACH, KILL, INTERRUPT, UNKNOWN };
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

#### 2.4 Continue/Step and Asynchronous Stop

After `c`/`s`:
- Main thread resumes emulator, does **not** enqueue response immediately
- TCP thread enters "waiting for stop" state
- Checks both response queue and socket (for Ctrl-C) using 100ms timeout
- Stop reply sent when breakpoint hit or step completes

#### 2.5 Ctrl-C Handling

Byte 0x03 detected by TCP thread at any time:
- If emulator halted: ignored
- If emulator running: enqueue INTERRUPT → main thread halts → stop reply `T02` (SIGINT)

#### 2.6 Shutdown Sequence

1. Main thread sets `std::atomic<bool> gdb_shutdown = true` [D5]
2. Close server socket (unblocks `accept()`)
3. TCP thread detects flag, closes client socket, exits
4. Main thread calls `std::thread::join()`

---

### 3. GDB RSP Packet Framing

#### 3.1 Packet Format

`$<data>#<two-hex-digit-checksum>`

Checksum = sum of all bytes in `<data>` mod 256.

#### 3.2 Parsing State Machine

```
States: IDLE → PACKET_DATA → CHECKSUM_1 → CHECKSUM_2

IDLE + '$'  → PACKET_DATA (reset buffer)
IDLE + 0x03 → emit INTERRUPT (Ctrl-C)
IDLE + '+'  → ACK received (ignore in NoAckMode)
IDLE + '-'  → NACK (retransmit last packet)

PACKET_DATA + '#'  → CHECKSUM_1
PACKET_DATA + '}'  → set escape flag (next byte XOR 0x20)
PACKET_DATA + byte → append to buffer

CHECKSUM_1 + hex → store, → CHECKSUM_2
CHECKSUM_2 + hex → validate checksum
```

Valid checksum → send `+`, dispatch. Invalid → send `-`, discard.

#### 3.3 NoAckMode

Supported via `QStartNoAckMode` [D6]. After `OK` response, both sides stop ACK/NACK.

#### 3.4 Escape Handling

Bytes `$`, `#`, `}`, `*` escaped as: `}` prefix + (byte XOR 0x20).

#### 3.5 Packet Buffer Sizing

Largest packet: 64KB memory read = 131072 hex chars + framing = ~131077 bytes.

Use `std::string` (dynamic) [D7]. Report `PacketSize=20000` (hex for 131072) in `qSupported`.

---

### 4. Command Parser & Dispatcher

| Prefix | Command | Response |
|--------|---------|----------|
| `?` | Stop reason | `T05` (SIGTRAP — halt on connect) |
| `g` | Read all registers | 14 hex chars: `AAXXYYSSPPPPFF` |
| `G` | Write all registers | `OK` |
| `p n` | Read register n | 2 or 4 hex chars |
| `P n=v` | Write register n | `OK` |
| `m addr,len` | Read memory | Hex-encoded bytes |
| `M addr,len:data` | Write memory | `OK` |
| `s [addr]` | Single step | `T05` (after instruction completes) |
| `c [addr]` | Continue | `T05` on breakpoint, `T02` on Ctrl-C |
| `Z0,addr,kind` | Set SW breakpoint | `OK` |
| `z0,addr,kind` | Clear SW breakpoint | `OK` |
| `Z1,addr,kind` | Set HW breakpoint | `OK` (same as Z0) [D10] |
| `z1,addr,kind` | Clear HW breakpoint | `OK` (same as z0) [D10] |
| `D` | Detach | `OK`, resume, disconnect |
| `k` | Kill | Resume, disconnect [D12] |
| `Hg`/`Hc` | Thread select | `OK` (single-threaded) |
| `qSupported` | Capabilities | `PacketSize=20000;QStartNoAckMode+;qXfer:features:read+` |
| `qAttached` | Attached query | `1` |
| `qXfer:features:read:target.xml:off,len` | Target XML | Chunked XML |
| `qfThreadInfo` | Thread list start | `m1` |
| `qsThreadInfo` | Thread list cont | `l` |
| `qC` | Current thread | `QC1` |
| `vMustReplyEmpty` | Unknown v-packet | empty |
| `vCont?` | vCont query | empty [D11] |
| unknown | Unknown command | empty (`$#00`) |

#### Error Responses

| Code | Meaning |
|------|---------|
| `E01` | Bad address / memory access error |
| `E02` | Bad register number |
| `E03` | Malformed command / parse error |

#### Register Layout (GDB register numbers)

| Reg# | Name | Size | Encoding |
|------|------|------|----------|
| 0 | A | 8-bit | 2 hex chars |
| 1 | X | 8-bit | 2 hex chars |
| 2 | Y | 8-bit | 2 hex chars |
| 3 | SP | 8-bit | 2 hex chars |
| 4 | PC | 16-bit | 4 hex chars, little-endian |
| 5 | P (flags) | 8-bit | 2 hex chars |

`g` response example: A=0x42, X=0x00, Y=0xFF, SP=0xFD, PC=0xD000, P=0x24 → `420000fffd00d024`

---

### 5. 6502 Target Description XML

```xml
<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target version="1.0">
  <architecture>6502</architecture>
  <feature name="org.gnu.gdb.m6502.cpu">
    <reg name="a"     bitsize="8"  type="uint8"    regnum="0"/>
    <reg name="x"     bitsize="8"  type="uint8"    regnum="1"/>
    <reg name="y"     bitsize="8"  type="uint8"    regnum="2"/>
    <reg name="sp"    bitsize="8"  type="uint8"    regnum="3"/>
    <reg name="pc"    bitsize="16" type="code_ptr"  regnum="4"/>
    <reg name="flags" bitsize="8"  type="uint8"    regnum="5"/>
  </feature>
</target>
```

SP is raw 8-bit (not 0x01xx). PC little-endian. GDB has no native 6502 disassembly [D13].

Served via `qXfer:features:read:target.xml:offset,length` with `l`/`m` prefix for last/more chunks.

---

### 6. Packet Flow Examples

#### Initial Connection
```
GDB connects → stub halts emulator
GDB → $qSupported:...#xx
Stub → +$PacketSize=20000;QStartNoAckMode+;qXfer:features:read+#xx
GDB → +$QStartNoAckMode#xx
Stub → +$OK#xx
(no more ACKs)
GDB → $qXfer:features:read:target.xml:0,fff#xx
Stub → $l<?xml ...>#xx
GDB → $?#xx
Stub → $T05#xx
GDB → $Hg0#xx
Stub → $OK#xx
GDB → $g#xx
Stub → $420000fffd00d024#xx
```

#### Set Breakpoint and Continue
```
GDB → $Z0,d000,1#xx
Stub → $OK#xx
GDB → $c#xx
... emulator runs, hits BP at 0xD000 ...
Stub → $T05#xx
```

#### Ctrl-C Interrupt
```
GDB → $c#xx
... emulator running ...
GDB → 0x03 (raw byte)
Stub → $T02#xx (SIGINT)
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

    // Reset
    void     (*reset)(void);
};
```

#### 1.2 Callback → Emulator Mapping

New accessor functions added to `emulator.h`/`emulator.cpp` [D14]:

| Callback | Emulator Implementation |
|----------|------------------------|
| `read_reg8(0)` | `emulator_read_a()` → `m6502_a(&cpu)` |
| `read_reg8(1)` | `emulator_read_x()` → `m6502_x(&cpu)` |
| `read_reg8(2)` | `emulator_read_y()` → `m6502_y(&cpu)` |
| `read_reg8(3)` | `emulator_read_s()` → `m6502_s(&cpu)` |
| `read_reg8(5)` | `emulator_read_p()` → `m6502_p(&cpu)` |
| `read_reg16(4)` | `emulator_getpc()` → `m6502_pc(&cpu)` (already exists) |
| `write_reg8(n)` | `emulator_write_a/x/y/s/p()` → `m6502_set_a/x/y/s/p(&cpu, v)` |
| `write_reg16(4)` | `emulator_write_pc()` → full prefetch sequence [D16] |
| `read_mem(addr)` | `return mem[addr]` — direct, no bus decode [D17] |
| `write_mem(addr, val)` | `mem[addr] = val` — direct, ROM writable [D18, D19] |
| `set_breakpoint(addr)` | `bp_mask[addr] = true; bp_enable = true` |
| `clear_breakpoint(addr)` | `bp_mask[addr] = false` |
| `step_instruction()` | Loop `emulator_step()` until `pins & M6502_SYNC` [D20] |
| `continue_exec()` | Set `run_emulator = true` |
| `halt()` | Set `run_emulator = false` |
| `get_stop_reason()` | Return 5 (SIGTRAP) or 4 (SIGILL for JAM) [D21] |
| `get_pc()` | `emulator_getpc()` |
| `reset()` | `emulator_reset()` |

### 2. Register Access

#### 2.1 m6502.h Getter/Setter Pairs (confirmed)

- Getters: `m6502_a()`, `m6502_x()`, `m6502_y()`, `m6502_s()`, `m6502_p()`, `m6502_pc()`
- Setters: `m6502_set_a()`, `m6502_set_x()`, `m6502_set_y()`, `m6502_set_s()`, `m6502_set_p()`, `m6502_set_pc()`

All setters directly write CPU struct fields. No side effects except PC.

#### 2.2 PC Write — Full Prefetch Sequence [D16]

Writing PC requires resetting the fetch state machine:
```cpp
void emulator_write_pc(uint16_t addr) {
    pins = M6502_SYNC;
    M6502_SET_ADDR(pins, addr);
    M6502_SET_DATA(pins, mem[addr]);
    m6502_set_pc(&cpu, addr);
}
```

This ensures the next `m6502_tick()` begins a clean opcode fetch regardless of prior CPU state.

#### 2.3 Flags Register (P)

Standard 6502 bit layout: `NV-BDIZC` (bits 7-0). Read/write atomically via `m6502_p()`/`m6502_set_p()`.

### 3. Memory Access

- Direct `mem[]` read/write — bypasses bus decode [D17]
- No device side-effects on read (TTY registers, frame buffer)
- ROM area ($D000+) writable — useful for debug patching [D19]
- Address range: `uint16_t` naturally constrains to 0x0000-0xFFFF
- No validation needed — `mem[]` is exactly 65536 bytes

### 4. Instruction-Level Stepping

#### 4.1 Instruction Boundary Detection [D20]

`m6502.h` signals boundaries via the `M6502_SYNC` pin. The `_FETCH()` macro at the end of each instruction sets SYNC. Verified in source:
- NOP (0xEA): 2 states, `_FETCH()` at state 1
- LDA abs (0xAD): 4 states, `_FETCH()` at state 3
- BRK (0x00): 7 states, `_FETCH()` at state 6

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

#### 4.3 Initial Halt Alignment [D23]

When GDB first connects and the emulator is halted mid-instruction, advance to the next SYNC boundary before processing any GDB commands:
```
on_gdb_connect():
    if !(pins & M6502_SYNC):
        guard = 0
        while !(pins & M6502_SYNC) && guard < 16:
            emulator_step()
            guard++
```

#### 4.4 Edge Cases

- **BRK**: 7 cycles, completes normally, lands at IRQ handler. Report SIGTRAP.
- **JAM opcodes** (0x02, 0x12, etc.): Loop forever via `c->IR--`. Guard counter fires → SIGILL.
- **Interrupt during step**: IRQ/NMI forces BRK sequence (7 extra cycles). Guard of 16 handles worst case.

### 5. Continue and Breakpoint Detection

#### 5.1 Run State

`run_emulator` promoted from function-local static to externally visible variable. Both ImGui buttons and GDB callbacks read/write it.

#### 5.2 Breakpoint SYNC Fix [D32]

Modify `emulator_step()` breakpoint check to only fire on instruction fetch:
```cpp
// Before (fires on any memory access):
if(bp_enable && bp_mask[addr]) { bp_hit = true; ... }

// After (fires only on opcode fetch):
if(bp_enable && bp_mask[addr] && (pins & M6502_SYNC)) { bp_hit = true; ... }
```

This is a correctness fix for both GDB and the existing ImGui debugger.

#### 5.3 Stop Notification [D29]

When emulator hits a breakpoint during `continue`:
1. Main loop detects via `emulator_check_break()`
2. Sets `run_emulator = false`, `gdb_halted = true`
3. Calls `gdb_stub_notify_stop(5)` — enqueues `T05` to response queue
4. TCP thread sends stop reply to GDB

### 6. Device Interaction During Debug

- **TTY**: Does not tick when halted. Ticks normally during step [D39, D41].
- **Frame buffer**: Frozen when halted. Updated during step.
- **IRQ**: Frozen while halted. Resumes on continue [D40].
- **stdin/TTY conflict**: None — GDB uses TCP, not stdin.

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
    GDB_POLL_NONE     = 0,  // nothing happened
    GDB_POLL_HALTED   = 1,  // GDB requested halt (or client connected)
    GDB_POLL_RESUMED  = 2,  // GDB sent 'c' (continue)
    GDB_POLL_STEPPED  = 3,  // GDB sent 's' (step completed)
    GDB_POLL_DETACHED = 4,  // GDB detached/disconnected
    GDB_POLL_KILL     = 5,  // GDB sent 'k'
};

gdb_poll_result_t gdb_stub_poll(void);
```

Early-returns `GDB_POLL_NONE` if no client connected (single atomic load) [D33].

#### 2.3 Status and Notification

```cpp
bool gdb_stub_is_connected(void);
bool gdb_stub_is_halted(void);
void gdb_stub_notify_stop(uint8_t signal);  // for breakpoint-during-run [D29]

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
// ... etc.
#endif
```

### 3. Main Loop Integration

#### 3.1 Placement

`gdb_stub_poll()` inserted **before** the emulation tick block, at the top of the main loop body.

#### 3.2 State Variables

```cpp
static bool run_emulator = false;    // existing (promoted to visible scope)
static bool step_emulator = false;   // existing
static bool gdb_halted = false;      // NEW
```

Emulator ticks only when `run_emulator == true && gdb_halted == false`.

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

#### 3.4 ImGui Conflict Resolution [D30]

| Scenario | Behavior |
|----------|----------|
| GDB connected, user presses Run | `gdb_halted` gates tick block → no effect |
| GDB connected, user presses Step | Same — gated |
| GDB sends `c` while user had stopped | GDB overrides: `run_emulator=true` |
| User presses Reset while GDB connected | Allowed; call `gdb_stub_notify_stop(5)` after |
| GDB disconnects | `gdb_halted=false`; emulator returns to prior state |

Run/Step buttons wrapped in `ImGui::BeginDisabled(gdb_halted)` [D30].
Status text: "Halted (GDB)" when `gdb_halted && gdb_stub_is_connected()` [D31].

### 4. Build Integration

#### 4.1 Makefile Changes

```makefile
SOURCES += $(SRC_DIR)/gdb_stub.cpp

# Linux:
LIBS += -lpthread
```

Existing pattern rule handles new `.cpp` files automatically.

#### 4.2 Platform [D36]

Linux/macOS only. Windows Winsock deferred (TODO comments in source).

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
| `recv()` returns 0 | Clean disconnect → resume emulator → re-accept |
| `send()` fails | Treat as disconnect |
| TCP thread exception | Caught by try/catch. Log. Thread continues. |

### 7. ImGui Integration [D38]

Minimal changes:
- Status line in existing "Emulator Control" window: `GDB: Connected (port 3333)` / `GDB: Listening`
- Log callback wired to `gui_con_printmsg()` for connect/disconnect/error events
- No new ImGui windows

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
```

#### T04: Ctrl-C detection (0x03 outside packet)
```
State: IDLE
Input byte: 0x03
Expected: INTERRUPT event emitted, no packet parsed
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
1. Stub sends: $T05#b9
2. GDB replies: - (NACK)
3. Expected: stub retransmits $T05#b9
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
Verify: PC is little-endian (0xD000 → "00d0")
```

#### T12: Read single register (`p`)
```
Send: $p0#XX   → A register
Expected: $42#XX (2 hex chars)

Send: $p4#XX   → PC register
Expected: $00d0#XX (4 hex chars, little-endian)
```

#### T13: Write single register (`P`)
```
Send: $P0=ff#XX   → write A=0xFF
Expected: $OK#XX
Verify: callback reports A=0xFF after write
```

#### T14: Write PC register (`P4`)
```
Send: $P4=00e0#XX   → write PC=0xE000 (little-endian: 00e0)
Expected: $OK#XX
Verify: PC reads back as 0xE000
Verify: full prefetch sequence was used (M6502_SYNC set, addr/data bus updated)
```

#### T15: Write all registers (`G`)
```
Send: $G001122330040a5#XX
Expected: $OK#XX
Verify: A=0x00, X=0x11, Y=0x22, SP=0x33, PC=0x4000 (LE: 0040), P=0xA5
```

#### T16: Invalid register number
```
Send: $p6#XX   → register 6 doesn't exist
Expected: $E02#XX (bad register number)
```

#### T17: Flags register bit verification
```
Setup: P = 0xA5 = 10100101 → N=1 V=0 -=1 B=0 D=0 I=1 Z=0 C=1
Send: $p5#XX
Expected: $a5#XX
Verify: each bit maps correctly to NV-BDIZC
```

### 4.3 Memory Access

#### T18: Read single byte
```
Setup: mem[0x0200] = 0xAB
Send: $m200,1#XX
Expected: $ab#XX
```

#### T19: Read range
```
Setup: mem[0x0200..0x0203] = {0xDE, 0xAD, 0xBE, 0xEF}
Send: $m200,4#XX
Expected: $deadbeef#XX
```

#### T20: Write single byte
```
Send: $M200,1:42#XX
Expected: $OK#XX
Verify: mem[0x0200] = 0x42
```

#### T21: Write range
```
Send: $M200,4:deadbeef#XX
Expected: $OK#XX
Verify: mem[0x0200..0x0203] = {0xDE, 0xAD, 0xBE, 0xEF}
```

#### T22: Read address boundaries
```
Send: $m0,1#XX          → address 0x0000
Expected: $XX#XX (first byte of memory)

Send: $mffff,1#XX       → address 0xFFFF
Expected: $XX#XX (last byte of memory)
```

#### T23: Read wrapping past end (error case)
```
Send: $mffff,2#XX       → would read 0xFFFF and 0x10000
Expected: $E01#XX (address out of range)
```

#### T24: Write to ROM area ($D000+)
```
Setup: mem[0xD000] = 0x4C (original ROM byte)
Send: $Md000,1:EA#XX    → write NOP over ROM
Expected: $OK#XX
Verify: mem[0xD000] = 0xEA
Note: ROM is writable (no protection in emulator) [D19]
```

#### T25: Read device-mapped region (no side effects)
```
Setup: TTY input queue has data
Send: $mc103,1#XX       → TTY data register
Expected: returns mem[0xC103] (raw RAM), NOT pop from TTY queue [D17]
Verify: TTY queue length unchanged after read
```

#### T26: Full 64KB memory dump
```
Send: $m0,10000#XX
Expected: 131072 hex chars response
Verify: checksum correct
Verify: response matches entire mem[] array
```

### 4.4 Breakpoints

#### T27: Set and clear breakpoint
```
Send: $Z0,d000,1#XX    → set bp at 0xD000
Expected: $OK#XX
Verify: bp_mask[0xD000] == true, bp_enable == true

Send: $z0,d000,1#XX    → clear bp at 0xD000
Expected: $OK#XX
Verify: bp_mask[0xD000] == false
```

#### T28: Set multiple breakpoints
```
Send: $Z0,d000,1#XX    → bp at 0xD000
Send: $Z0,d010,1#XX    → bp at 0xD010
Send: $Z0,d020,1#XX    → bp at 0xD020
Expected: all three set, bp_enable == true
```

#### T29: Z1 (hardware bp) maps to same mechanism [D10]
```
Send: $Z1,d000,1#XX
Expected: $OK#XX
Verify: bp_mask[0xD000] == true (same as Z0)
```

#### T30: Unsupported breakpoint types
```
Send: $Z2,d000,1#XX    → write watchpoint
Expected: $#00 (empty — unsupported)

Send: $Z3,d000,1#XX    → read watchpoint
Expected: $#00

Send: $Z4,d000,1#XX    → access watchpoint
Expected: $#00
```

#### T31: Breakpoint fires only on SYNC (instruction fetch) [D32]
```
Setup: bp_mask[0x0200] = true, mem[0x0200] = 0x42
Instruction at 0xD000: LDA $0200 (reads data from 0x0200)
Run from 0xD000.
Expected: breakpoint does NOT fire (0x0200 accessed as data, not instruction fetch)
Verify: bp_hit remains false during the LDA data read cycle
```

#### T32: Breakpoint fires on instruction at BP address
```
Setup: bp_mask[0xD000] = true, instruction at 0xD000: NOP
Run from 0xCFFE (instruction before 0xD000).
Expected: breakpoint fires when PC reaches 0xD000 (SYNC cycle)
Verify: stop reply T05 sent
```

### 4.5 Execution Control — Stepping

#### T33: Step NOP (2 cycles)
```
Setup: PC at 0xD000, mem[0xD000] = 0xEA (NOP)
Send: $s#XX
Expected: $T05#XX
Verify: PC now at 0xD001
Verify: exactly 2 calls to emulator_step()
Verify: pins & M6502_SYNC is true after step
```

#### T34: Step LDA absolute (4 cycles)
```
Setup: PC at 0xD000, mem = [0xAD, 0x00, 0x02, ...] (LDA $0200)
       mem[0x0200] = 0x42
Send: $s#XX
Expected: $T05#XX
Verify: PC at 0xD003, A = 0x42
Verify: exactly 4 calls to emulator_step()
```

#### T35: Step BRK (7 cycles)
```
Setup: PC at 0xD000, mem[0xD000] = 0x00 (BRK)
       IRQ vector at 0xFFFE/0xFFFF = 0xE000
Send: $s#XX
Expected: $T05#XX
Verify: PC at 0xE000 (IRQ handler)
Verify: stack contains return address and flags
```

#### T36: Step JAM instruction (guard fires)
```
Setup: PC at 0xD000, mem[0xD000] = 0x02 (JAM/KIL)
Send: $s#XX
Expected: $T04#XX (SIGILL — illegal instruction) [D21]
Verify: guard counter hit limit (16 cycles)
```

#### T37: Step with address parameter
```
Setup: PC at 0xD000
Send: $sd010#XX         → step, but set PC to 0xD010 first
Expected: $T05#XX
Verify: instruction at 0xD010 was executed, not 0xD000
```

#### T38: Multiple sequential steps
```
Setup: PC at 0xD000, three NOPs at 0xD000-0xD002
Send: $s#XX → verify PC=0xD001
Send: $s#XX → verify PC=0xD002
Send: $s#XX → verify PC=0xD003
Verify: each step returns T05, each advances PC by 1
```

### 4.6 Execution Control — Continue

#### T39: Continue then breakpoint hit
```
Setup: PC at 0xD000, bp at 0xD010
       Instructions 0xD000-0xD00F are NOPs, 0xD010 is NOP
Send: $c#XX
Expected: no immediate response
... emulator runs through NOPs ...
... hits bp at 0xD010 ...
Expected: $T05#XX (async stop reply)
Verify: PC = 0xD010
```

#### T40: Continue with address parameter
```
Setup: PC at 0xD000, bp at 0xD020
Send: $cd010#XX         → continue from 0xD010
Expected: emulator runs from 0xD010, hits bp at 0xD020
Receive: $T05#XX
Verify: PC = 0xD020
```

#### T41: Continue then Ctrl-C interrupt
```
Setup: PC at 0xD000, infinite loop at 0xD000: JMP $D000
Send: $c#XX
... emulator running (infinite loop) ...
Send: 0x03 (Ctrl-C)
Expected: $T02#XX (SIGINT)
Verify: emulator halted
Verify: PC somewhere in the loop (0xD000-ish)
```

#### T42: Continue with no breakpoints (must use Ctrl-C to stop)
```
Setup: PC at 0xD000, no breakpoints, infinite loop
Send: $c#XX
... runs forever ...
Send: 0x03
Expected: $T02#XX
```

### 4.7 Connection and Lifecycle

#### T43: Connect halts emulator [D23]
```
Setup: emulator running freely
Action: GDB connects via TCP
Expected: emulator halts
Expected: CPU at instruction boundary (pins & M6502_SYNC)
Verify: gdb_stub_poll() returns GDB_POLL_HALTED
```

#### T44: Disconnect resumes emulator
```
Setup: GDB connected, emulator halted
Action: GDB disconnects (TCP close)
Expected: gdb_stub_poll() returns GDB_POLL_DETACHED
Verify: gdb_halted = false
```

#### T45: Reconnect after disconnect
```
Setup: GDB was connected, then disconnected. Emulator running.
Action: new GDB connects
Expected: emulator halts again (fresh state)
Expected: no carryover from previous session [D34]
```

#### T46: Detach command
```
Send: $D#XX
Expected: $OK#XX
Verify: emulator resumes, GDB disconnected
Verify: next gdb_stub_poll() returns GDB_POLL_DETACHED
```

#### T47: Kill command [D12]
```
Send: $k#XX
Expected: no response (GDB doesn't expect one)
Verify: emulator resumes (not shut down)
Verify: gdb_stub_poll() returns GDB_POLL_KILL
```

#### T48: No-client overhead
```
Setup: gdb_stub_init() called, no GDB client connected
Measure: time for gdb_stub_poll()
Expected: returns GDB_POLL_NONE in < 1 microsecond
Verify: single atomic load, no mutex operations [D33]
```

#### T49: Init failure — port in use
```
Setup: another process bound to port 3333
Action: gdb_stub_init(port=3333)
Expected: returns -1
Verify: emulator runs normally without GDB
Verify: gdb_stub_poll() returns GDB_POLL_NONE (safe)
```

#### T50: Shutdown while connected
```
Setup: GDB connected, emulator halted
Action: gdb_stub_shutdown()
Expected: TCP thread joins cleanly
Expected: client socket closed (GDB sees disconnect)
Verify: no crash, no resource leak
```

### 4.8 Query Commands

#### T51: qSupported handshake
```
Send: $qSupported:multiprocess+;swbreak+;hwbreak+#XX
Expected: $PacketSize=20000;QStartNoAckMode+;qXfer:features:read+#XX
```

#### T52: Target XML transfer
```
Send: $qXfer:features:read:target.xml:0,fff#XX
Expected: response starts with 'l' (last chunk — XML < 4KB)
Expected: XML contains <architecture>6502</architecture>
Expected: XML contains 6 register definitions (a, x, y, sp, pc, flags)
```

#### T53: Target XML chunked transfer
```
Send: $qXfer:features:read:target.xml:0,10#XX   → only 16 bytes
Expected: response starts with 'm' (more data available)
Verify: response body is exactly 16 bytes of XML

Send: $qXfer:features:read:target.xml:10,fff#XX → rest
Expected: response starts with 'l' (last chunk)
```

#### T54: Thread info (single-threaded target)
```
Send: $qfThreadInfo#XX
Expected: $m1#XX

Send: $qsThreadInfo#XX
Expected: $l#XX
```

#### T55: qAttached
```
Send: $qAttached#XX
Expected: $1#XX (attached to existing process)
```

#### T56: Thread selection (any thread ID accepted)
```
Send: $Hg0#XX
Expected: $OK#XX

Send: $Hc-1#XX
Expected: $OK#XX
```

### 4.9 Integration Tests

#### T57: Full GDB handshake → inspect → step → continue → breakpoint
```
1. Connect
2. $qSupported → verify capabilities
3. $QStartNoAckMode → $OK
4. $? → $T05 (halted)
5. $g → read all registers
6. $m d000,10 → read 16 bytes of ROM
7. $Z0,d010,1 → set breakpoint
8. $s → step one instruction → $T05
9. $g → verify PC advanced
10. $c → continue → ... → $T05 (breakpoint hit)
11. $g → verify PC = 0xD010
12. $z0,d010,1 → clear breakpoint
13. $D → detach → $OK
```

#### T58: Memory write → verify via read
```
1. Connect
2. $Md000,4:deadbeef#XX → write 4 bytes
3. $md000,4#XX → read back
4. Verify response is "deadbeef"
```

#### T59: Register write → step → verify effect
```
Setup: NOP at 0xE000
1. $P4=00e0#XX → set PC to 0xE000
2. $s#XX → step one NOP
3. $p4#XX → read PC
4. Verify: PC = 0xE001 (little-endian: "01e0")
```

#### T60: Breakpoint at reset vector
```
1. Set bp at reset vector target
2. Send reset via callback
3. Continue
4. Verify: breaks at reset vector entry point
```

#### T61: Step through I/O instruction
```
Setup: instruction at 0xD000: STA $C000 (write to frame buffer)
       A = 0x42
1. Step
2. Verify: frame_buffer[0] = 0x42 (device ticked during step) [D39]
3. Verify: PC advanced past the STA
```

### 4.10 Edge Cases and Error Handling

#### T62: Malformed memory read (missing comma)
```
Send: $m200#XX (no length)
Expected: $E03#XX (parse error)
```

#### T63: Malformed register write
```
Send: $P#XX (no register number or value)
Expected: $E03#XX
```

#### T64: Zero-length memory read
```
Send: $m200,0#XX
Expected: $#XX (empty response — zero bytes)
```

#### T65: Rapid command sequence
```
Send: $g#XX → receive response
Send: $g#XX → receive response
Send: $g#XX → receive response
(3 rapid reads)
Expected: all three return identical register state (CPU is halted)
```

#### T66: Breakpoint at address 0x0000
```
Send: $Z0,0,1#XX
Expected: $OK#XX
Verify: bp_mask[0] == true
```

#### T67: Breakpoint at address 0xFFFF
```
Send: $Z0,ffff,1#XX
Expected: $OK#XX
Verify: bp_mask[0xFFFF] == true
```

#### T68: Double-set breakpoint (idempotent)
```
Send: $Z0,d000,1#XX → $OK
Send: $Z0,d000,1#XX → $OK (no error, already set)
```

#### T69: Clear non-existent breakpoint (idempotent)
```
Send: $z0,d000,1#XX → $OK (no error, nothing to clear)
```

#### T70: GDB disconnect mid-continue
```
1. $c#XX → emulator running
2. GDB closes TCP connection (no Ctrl-C, no detach)
3. Expected: TCP thread detects disconnect
4. Expected: emulator resumes (or stays running)
5. Expected: stub returns to accept() state
```

#### T71: Socket error during send
```
Setup: GDB connected
Simulate: network error on next send()
Expected: treated as disconnect
Expected: emulator resumes, stub re-accepts
```

#### T72: Concurrent ImGui Step while GDB halted [D30]
```
Setup: GDB connected, gdb_halted = true
Action: user clicks "Step" button in ImGui
Expected: no effect (gated by gdb_halted)
Verify: emulator_step() NOT called
```

---

## Implementation Files Summary

### New Files
- `src/gdb_stub.h` — public API, callback struct, config, lifecycle, poll
- `src/gdb_stub.cpp` — TCP thread, queues, packet framing, RSP protocol

### Modified Files
- `src/emulator.h` — add register accessor functions (read/write A,X,Y,S,P,PC)
- `src/emulator.cpp` — implement accessors, add SYNC check to bp detection [D32], add `clear_breakpoint()` function
- `src/main.cpp` — wire callbacks, call init/poll/shutdown, add `gdb_halted`, gate tick block, update ImGui status
- `Makefile` — add `gdb_stub.cpp` to SOURCES, add `-lpthread`

### Estimated Size
- `gdb_stub.h`: ~80 lines
- `gdb_stub.cpp`: ~500-600 lines
- `emulator.h` changes: ~20 lines
- `emulator.cpp` changes: ~40 lines
- `main.cpp` changes: ~50 lines
- **Total new/modified: ~700-800 lines**
