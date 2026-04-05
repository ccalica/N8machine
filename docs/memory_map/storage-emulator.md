# Storage Device — Emulator Implementation

Implementation guide for the N8Machine storage device. All commands implemented:
**LS, OPEN (R/W/A), CLOSE, PWDIR, SEEK, CHDIR, MKDIR, RMDIR, REMOVE, MOVE**.

Reference spec: `docs/memory_map/storage.md`

## Files to Create

| File | Purpose |
|------|---------|
| `src/emu_storage.h` | Header: function declarations + GDB accessor prototypes |
| `src/emu_storage.cpp` | Device implementation: state, decode, command parser, host FS |
| `test/test_storage.cpp` | Unit tests (doctest) |
| `firmware/gdb_playground/test_storage.s` | 6502 integration test |

## Files to Modify

| File | Change |
|------|--------|
| `src/n8_memory_map.h` | Add `N8_STORAGE_*` constants after Keyboard section |
| `src/emulator.cpp` | Wire `storage_init/reset/tick/decode`, add slot 4 case |
| `src/gdb_bridge.cpp` | Intercept storage register range in `gdb_read_mem()` |
| `src/main.cpp` | Parse `--storage <path>` CLI arg |
| `Makefile` | Add `emu_storage.cpp` to SOURCES and TEST_SRC_OBJS |

## Memory Map Constants

Add to `src/n8_memory_map.h` after the Keyboard section:

```c
// --- Storage Device (slot 4) ---
#define N8_STORAGE_BASE       0xD880
#define N8_DISK_CHAN          0x00    // Register offsets within slot
#define N8_DISK_DATA          0x01
#define N8_DISK_ERROR         0x02
#define N8_DISK_CTRL          0x03
#define N8_DISK_STATUS        0x04
#define N8_DISK_REG_COUNT     5
#define N8_STORAGE_SLOT       4

// DISK_CHAN read bits
#define N8_DISK_CHAN_MASK     0x0F
#define N8_DISK_CHAN_ACTIVE   0x20    // bit 5

// DISK_ERROR bits
#define N8_DISK_ERR_CMD       0x80   // bit 7: command error flag

// DISK_CTRL bits
#define N8_DISK_CTRL_PARSER   0x01   // bit 0: reset command parser
#define N8_DISK_CTRL_CHANNEL  0x02   // bit 1: reset current channel
#define N8_DISK_CTRL_DEVICE   0x80   // bit 7: full device reset

// DISK_STATUS bits
#define N8_DISK_STAT_AVAIL    0x0F   // bits 0-3 mask
#define N8_DISK_STAT_EOF      0x20   // bit 5
#define N8_DISK_STAT_CTS      0x40   // bit 6
#define N8_DISK_STAT_BUSY     0x80   // bit 7

// I/O error codes (DISK_ERROR bits 0-6)
#define N8_DISK_IOE_NONE          0x00
#define N8_DISK_IOE_NOT_OPEN      0x01
#define N8_DISK_IOE_DISK_FULL     0x02
#define N8_DISK_IOE_PAST_EOF      0x03
#define N8_DISK_IOE_PERMISSION    0x04
#define N8_DISK_IOE_NOT_READY     0x05
#define N8_DISK_IOE_CMD_OVERFLOW  0x06

// Command error codes (read from DISK_DATA when bit 7 set)
#define N8_DISK_CE_FILE_NOT_FOUND  0x01
#define N8_DISK_CE_FILE_EXISTS     0x02
#define N8_DISK_CE_DIR_NOT_FOUND   0x03
#define N8_DISK_CE_CHAN_NOT_OPEN   0x04
#define N8_DISK_CE_NO_FREE_CHAN    0x05
#define N8_DISK_CE_DISK_FULL       0x06
#define N8_DISK_CE_PERMISSION      0x08
#define N8_DISK_CE_BAD_SYNTAX      0x09
#define N8_DISK_CE_BAD_ARG         0x0A
#define N8_DISK_CE_NAME_TOO_LONG   0x0B
#define N8_DISK_CE_NOT_A_DIR       0x0C
#define N8_DISK_CE_DIR_NOT_EMPTY   0x0D
#define N8_DISK_CE_NOT_READY       0x0E

// Control channel ID
#define N8_DISK_CONTROL_CHAN  0x0F
```

## Header (emu_storage.h)

```c
#pragma once
#include <cstdint>

void storage_init();
void storage_reset();
void storage_tick();
void storage_decode(uint64_t& pins, uint8_t reg);

// Configuration
void storage_set_root_path(const char* path);

// GDB bridge accessors (no side effects)
uint8_t storage_get_chan();
uint8_t storage_get_error();
uint8_t storage_get_status();
```

No accessor for DISK_DATA — it has side effects (buffer consumption, auto-close).
No accessor for DISK_CTRL — write-only.

## Data Structures

All state is file-scoped static in `emu_storage.cpp`, following the existing
device pattern (`emu_video.cpp`, `emu_kbd.cpp`).

### Channel State

```cpp
#define STORAGE_MAX_CHANNELS  15     // 0x00-0x0E (0x0F = control)
#define STORAGE_READ_BUF_SIZE 256
#define STORAGE_CMD_BUF_SIZE  257    // 256 payload + null
#define STORAGE_PATH_MAX      1024

enum channel_type_t {
    CHAN_NONE = 0,       // free slot
    CHAN_FILE,           // OPEN — backed by host FILE*
    CHAN_RESPONSE        // LIST/PWDIR — backed by in-memory buffer, auto-close
};

enum channel_mode_t {
    MODE_NONE = 0,
    MODE_READ,
    MODE_WRITE,         // Phase 2
    MODE_APPEND          // Phase 2
};

struct channel_t {
    channel_type_t type;
    channel_mode_t mode;
    bool           active;

    // File channel (CHAN_FILE)
    FILE*          fp;
    char           host_path[STORAGE_PATH_MAX];  // for double-open detection

    // Response channel (CHAN_RESPONSE)
    uint8_t*       response_data;    // malloc'd, freed on close
    int            response_len;
    int            response_pos;     // read cursor into response_data

    // Read buffer (both channel types)
    uint8_t        read_buf[STORAGE_READ_BUF_SIZE];
    int            buf_pos;          // next byte to return
    int            buf_count;        // valid bytes remaining
    bool           eof;

    // Per-channel I/O error (bits 0-6 of DISK_ERROR)
    uint8_t        io_error;
};
```

### Device State

```cpp
static channel_t channels[STORAGE_MAX_CHANNELS];
static uint8_t   current_channel;      // 0x00-0x0F

// Command parser
static uint8_t   cmd_buf[STORAGE_CMD_BUF_SIZE];
static int       cmd_len;
static bool      cmd_overflow;
static bool      cmd_in_payload;       // true during SK binary payload
static int       cmd_expected_payload; // bytes remaining in binary payload

// Command result (set by command handlers)
static bool      cmd_error_flag;       // DISK_ERROR bit 7
static uint8_t   cmd_error_code;       // read from DISK_DATA when bit 7 set
static bool      cmd_result_valid;     // command returned a value
static uint8_t   cmd_result;           // the return value (e.g., channel_id)

// Control channel I/O error (e.g., buffer overflow)
static uint8_t   ctrl_io_error;

// Filesystem
static char      root_path[STORAGE_PATH_MAX];  // host directory
static char      cwd[STORAGE_PATH_MAX];        // guest working directory, "/" initially
```

## Register Decode Logic

### Overall Structure

```cpp
void storage_decode(uint64_t& pins, uint8_t reg) {
    if (reg >= N8_DISK_REG_COUNT) {
        // Phantom registers: read 0x00, write ignored
        if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
        return;
    }

    if (pins & M6502_RW) {
        // ---- READ ----
        switch (reg) { /* ... */ }
    } else {
        // ---- WRITE ----
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) { /* ... */ }
    }
}
```

### DISK_CHAN (reg 0x00)

**Write:**
```
uint8_t id = val & N8_DISK_CHAN_MASK;
current_channel = id;
if (id == N8_DISK_CONTROL_CHAN) {
    ctrl_io_error = 0;
    cmd_error_flag = false;
    cmd_error_code = 0;
} else if (id < STORAGE_MAX_CHANNELS) {
    channels[id].io_error = 0;
}
```

**Read:**
```
uint8_t result = current_channel & N8_DISK_CHAN_MASK;
if (current_channel < STORAGE_MAX_CHANNELS && channels[current_channel].active) {
    result |= N8_DISK_CHAN_ACTIVE;
}
M6502_SET_DATA(pins, result);
```

Control channel (0x0F) never reports Active.

### DISK_DATA (reg 0x01)

**Write — control channel:**

Feed byte into command parser (see Command Parser section below).

**Write — data channel:**

Phase 1: only read-mode OPEN is supported.
- If channel not active: set `io_error = N8_DISK_IOE_NOT_OPEN`.
- If channel mode is MODE_READ: set `io_error = N8_DISK_IOE_PERMISSION`.

**Read — control channel:**
```
if (cmd_error_flag) {
    M6502_SET_DATA(pins, cmd_error_code);
} else if (cmd_result_valid) {
    M6502_SET_DATA(pins, cmd_result);
    cmd_result_valid = false;  // consumed
} else {
    M6502_SET_DATA(pins, 0x00);
}
```

**Read — data channel:**
```
channel_t& ch = channels[current_channel];
if (!ch.active) {
    ch.io_error = N8_DISK_IOE_NOT_OPEN;
    M6502_SET_DATA(pins, 0x00);
    return;
}
if (ch.mode != MODE_READ) {
    ch.io_error = N8_DISK_IOE_PERMISSION;
    M6502_SET_DATA(pins, 0x00);
    return;
}
if (ch.buf_count == 0 && ch.eof) {
    ch.io_error = N8_DISK_IOE_PAST_EOF;
    M6502_SET_DATA(pins, 0x00);
    return;
}
// Refill if buffer empty but not EOF
if (ch.buf_count == 0 && !ch.eof) {
    storage_refill_buffer(current_channel);
}
if (ch.buf_count > 0) {
    uint8_t byte = ch.read_buf[ch.buf_pos++];
    ch.buf_count--;
    M6502_SET_DATA(pins, byte);

    // Eagerly refill so bytes_avail in STATUS is accurate
    if (ch.buf_count == 0 && !ch.eof) {
        storage_refill_buffer(current_channel);
    }
    // Auto-close response channels when fully consumed
    if (ch.buf_count == 0 && ch.eof && ch.type == CHAN_RESPONSE) {
        storage_close_channel(current_channel);
    }
} else {
    M6502_SET_DATA(pins, 0x00);
}
```

### DISK_ERROR (reg 0x02)

**Read:**
```
if (current_channel == N8_DISK_CONTROL_CHAN) {
    uint8_t err = ctrl_io_error;
    if (cmd_error_flag) err = N8_DISK_ERR_CMD;
    M6502_SET_DATA(pins, err);
} else if (current_channel < STORAGE_MAX_CHANNELS) {
    M6502_SET_DATA(pins, channels[current_channel].io_error);
} else {
    M6502_SET_DATA(pins, 0x00);
}
```

**Write:** Ignored (read-only).

### DISK_CTRL (reg 0x03)

**Write (edge-triggered):**
```
if (val & N8_DISK_CTRL_DEVICE) {
    storage_reset();
    return;
}
if (val & N8_DISK_CTRL_PARSER) {
    cmd_len = 0;
    cmd_overflow = false;
    cmd_in_payload = false;
    cmd_error_flag = false;
    cmd_error_code = 0;
    cmd_result_valid = false;
    ctrl_io_error = 0;
    // Close all response channels
    for (int i = 0; i < STORAGE_MAX_CHANNELS; i++) {
        if (channels[i].type == CHAN_RESPONSE && channels[i].active)
            storage_close_channel(i);
    }
}
if (val & N8_DISK_CTRL_CHANNEL) {
    if (current_channel >= STORAGE_MAX_CHANNELS ||
        !channels[current_channel].active) {
        // Error on the channel (or control channel if 0x0F selected)
        if (current_channel < STORAGE_MAX_CHANNELS)
            channels[current_channel].io_error = N8_DISK_IOE_NOT_OPEN;
    } else {
        storage_close_channel(current_channel);
    }
}
```

**Read:** Returns 0x00 (write-only).

### DISK_STATUS (reg 0x04)

**Read:**
```
if (current_channel >= STORAGE_MAX_CHANNELS ||
    !channels[current_channel].active) {
    M6502_SET_DATA(pins, 0x00);  // undefined per spec
    return;
}
channel_t& ch = channels[current_channel];
uint8_t status = 0;

int avail = ch.buf_count;
if (avail > 15) avail = 15;
status |= (avail & N8_DISK_STAT_AVAIL);

if (ch.eof && ch.buf_count == 0)
    status |= N8_DISK_STAT_EOF;

status |= N8_DISK_STAT_CTS;  // always set

// Busy: buf empty, not EOF. Unreachable with synchronous refill,
// but architecturally correct for future async backing stores.
if (ch.buf_count == 0 && !ch.eof)
    status |= N8_DISK_STAT_BUSY;

M6502_SET_DATA(pins, status);
```

**Write:** Ignored (read-only).

## Command Parser

Bytes written to DISK_DATA on the control channel feed the command parser.

### Parse Flow (per byte)

```
1. If cmd_overflow: ignore byte, return.

2. If cmd_in_payload:
   - Append byte to cmd_buf (even if $00 — binary safe).
   - cmd_len++, cmd_expected_payload--.
   - If cmd_expected_payload == 0: set cmd_in_payload = false.
   - Check overflow: if cmd_len > 256, set overflow error.
   - Return.

3. If byte == $00 (null terminator):
   - If cmd_len == 0: ignore (double-null / idle parser).
   - Else: cmd_buf[cmd_len] = 0. Call storage_execute_command().

4. Normal byte:
   - Append to cmd_buf[cmd_len++].
   - If cmd_len > 256: set cmd_overflow = true,
     ctrl_io_error = N8_DISK_IOE_CMD_OVERFLOW, reset parser vars.
   - Opcode detection: when cmd_len == 3 and cmd_buf matches "SK,":
     set cmd_in_payload = true, cmd_expected_payload = 3.
```

### Opcode Detection

After accumulating 3 bytes, check if the buffer starts with a known
fixed-payload opcode + comma. Binary-payload opcodes: SK (3 bytes),
CL (1 byte). This is where the "smart parser" lives — extend this
check when adding future binary-payload commands.

```cpp
static void parser_check_binary_opcode() {
    if (cmd_len == 3 && cmd_buf[2] == ',') {
        if (cmd_buf[0] == 'S' && cmd_buf[1] == 'K') {
            cmd_in_payload = true;
            cmd_expected_payload = 3;  // type + lo + hi
        } else if (cmd_buf[0] == 'C' && cmd_buf[1] == 'L') {
            cmd_in_payload = true;
            cmd_expected_payload = 1;  // channel ID byte
        }
    }
}
```

### Command Dispatch

```cpp
static void storage_execute_command() {
    // Reset parser for next command
    int saved_len = cmd_len;
    cmd_len = 0;
    cmd_in_payload = false;
    cmd_overflow = false;

    // Clear previous result
    cmd_error_flag = false;
    cmd_error_code = 0;
    cmd_result_valid = false;
    cmd_result = 0;

    // Parse 2-char opcode
    if (saved_len < 2) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    char op0 = cmd_buf[0], op1 = cmd_buf[1];
    const char* args = (const char*)&cmd_buf[2];  // rest after opcode

    if      (op0=='L' && op1=='S') cmd_list(args);
    else if (op0=='O' && op1=='P') cmd_open(args);
    else if (op0=='C' && op1=='L') cmd_close(args);
    else if (op0=='P' && op1=='D') cmd_pwdir(args);
    else if (op0 == 'S' && op1 == 'K') cmd_seek(args);
    else if (op0 == 'C' && op1 == 'D') cmd_chdir(args);
    else if (op0 == 'M' && op1 == 'D') cmd_mkdir(args);
    else if (op0 == 'R' && op1 == 'D') cmd_rmdir(args);
    else if (op0 == 'R' && op1 == 'M') cmd_remove(args);
    else if (op0 == 'M' && op1 == 'V') cmd_move(args);
    else {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
    }
}
```

## Helper Functions

### storage_alloc_channel()

Scan `channels[0..14]` for first `active == false` slot. Return index, or
-1 if all occupied (caller sets command error `$05`).

### storage_close_channel(int id)

```
if (channels[id].type == CHAN_FILE && channels[id].fp)
    fclose(channels[id].fp);
if (channels[id].type == CHAN_RESPONSE && channels[id].response_data)
    free(channels[id].response_data);
memset(&channels[id], 0, sizeof(channel_t));
```

### storage_resolve_path(const char* guest_path, char* out, size_t out_size)

Converts a guest path to a host filesystem path:

1. If `guest_path` starts with `/`: absolute. Join `root_path + guest_path`.
2. Else: relative. Join `root_path + cwd + "/" + guest_path`.
3. **Security**: reject any component that is `..`. After joining, call
   `realpath()` on the result and verify it starts with `root_path`.
   This prevents guest programs from escaping the storage sandbox.
4. Return true on success, false on invalid/escaped path.

### storage_refill_buffer(int chan_id)

```
channel_t& ch = channels[chan_id];
if (ch.eof) return;

if (ch.type == CHAN_FILE) {
    size_t n = fread(ch.read_buf, 1, STORAGE_READ_BUF_SIZE, ch.fp);
    ch.buf_pos = 0;
    ch.buf_count = (int)n;
    if (n < STORAGE_READ_BUF_SIZE)
        ch.eof = true;
} else if (ch.type == CHAN_RESPONSE) {
    int remaining = ch.response_len - ch.response_pos;
    int to_copy = (remaining < STORAGE_READ_BUF_SIZE) ? remaining : STORAGE_READ_BUF_SIZE;
    memcpy(ch.read_buf, ch.response_data + ch.response_pos, to_copy);
    ch.response_pos += to_copy;
    ch.buf_pos = 0;
    ch.buf_count = to_copy;
    if (ch.response_pos >= ch.response_len)
        ch.eof = true;
}
```

### storage_check_already_open(const char* resolved_path)

Iterate all active `CHAN_FILE` channels. Compare `host_path` fields.
Return true if match found. Used by OPEN to enforce no-double-open.

## Command Implementations (Phase 1)

### PWDIR — `"PD"`

Simplest command. Good first implementation target.

1. Reject if args is non-empty (command error `$09`).
2. Allocate response channel. If no free channel, error `$05`.
3. Copy `cwd` string into `response_data` (malloc, memcpy).
4. Set `response_len = strlen(cwd)`.
5. Call `storage_refill_buffer()` to prime the read buffer.
6. Set `cmd_result = channel_id`, `cmd_result_valid = true`.

### LIST — `"LS,<path>"` or `"LS"`

1. Parse path from args. Default to `"."` if no arg.
2. Resolve to host path via `storage_resolve_path()`.
3. `opendir()` the resolved path. If it fails, try `stat()` for single file.
   If both fail, command error `$01`.
4. Build response buffer in memory. For each entry from `readdir()`:
   - Skip `.` and `..`.
   - `stat()` each entry for type and size.
   - Format: `D|F \t <name> \t <size_lo> <size_hi> \n`
   - Size is LE uint16. Saturate at `$FFFF` for files > 65535 bytes.
   - Sort: directories first, then files, alphabetical within each group.
5. Allocate response channel. Wire up `response_data`, `response_len`.
6. `storage_refill_buffer()` to prime read buffer.
7. Set `cmd_result = channel_id`.

**Implementation note**: sort requires collecting all entries before writing
the response. Use a growable buffer (realloc) or two-pass (count then fill).

### OPEN — `"OP,<mode>,<filename>"`

Modes: `R` (read), `W` (write/create/truncate), `A` (append/create).

1. Parse mode char at `args[1]`. Verify comma at `args[0]` and `args[2]`.
   If mode not `R`/`W`/`A`, error `$0A`. If syntax bad, error `$09`.
2. Extract filename from `args + 3`.
3. Validate filename length (max 63). Error `$0B` if too long.
4. `storage_resolve_path()` on filename. Error `$01` if invalid.
5. `storage_check_already_open()`. Error `$02` if already open.
6. `fopen(resolved, fmode)`. R→`"rb"`, W→`"wb"`, A→`"ab"`. Error `$01` if fails.
7. Allocate file channel. Error `$05` if none free.
8. Set `type = CHAN_FILE`, `mode`, `active = true`.
9. Store `resolved` in `host_path` for double-open detection.
10. Read mode only: `storage_refill_buffer()` to prime read buffer.
11. Set `cmd_result = channel_id`.

### CLOSE — `"CL,<chan_id>"`

1. Parse channel ID as binary byte from `args[1]`. Expect comma at `args[0]`.
   Channel ID is a raw byte (0x00-0x0E), not ASCII. The parser treats CL as a
   binary-payload opcode (1 byte after comma), same framing as SK.
   Error `$09` if bad syntax, `$0A` if out of range.
2. If channel not active, error `$04`.
3. Call `storage_close_channel(id)`.
4. Set `cmd_result = 0x00` (success), `cmd_result_valid = true`.

## Phase 2 Command Implementations

### OPEN Write/Append — `"OP,W,<filename>"` / `"OP,A,<filename>"`

Same validation as OPEN R (syntax, filename length, path resolution, double-open check).
Mode dispatch: `W` → `fopen(resolved, "wb")` (create/truncate), `A` → `fopen(resolved, "ab")` (create/append).
Write/append channels skip `storage_refill_buffer()` (no read buffer needed).
`DISK_DATA` write on data channel: `fwrite(&val, 1, 1, ch.fp)`. If fwrite fails, set `io_error = DISK_FULL`.
Reads on write/append channels return `PERMISSION` error (existing code handles this).

### SEEK — `"SK,<chan_id>,<type>,<lo><hi>"`

Binary payload (6 bytes after first comma): chan_id, comma, type, comma, lo, hi.
Channel ID is explicit — no implicit state tracking needed.

- `chan_id`: data channel (0x00-0x0E), binary byte (can be $00).
- `type`: `A` (absolute), `+` (forward), `-` (backward).
- `lo`, `hi`: LE uint16 offset, binary bytes.
- Reject if target channel is not an active CHAN_FILE → `CHAN_NOT_OPEN`.
- Reject MODE_APPEND → `BAD_ARG`.
- **Read channels**: compute logical position as `ftell(fp) - buf_count`, apply seek type
  to get absolute target, `fseek(SEEK_SET, target)`, invalidate buffer and refill.
- **Write channels**: `fseek` directly (no buffer offset issue).
- Returns `$00` on success.

### CHDIR — `"CD,<path>"`

1. Parse comma + path, resolve via `storage_resolve_path()`.
2. `stat()` resolved path. Error `$03` if not found, `$0C` if not a directory.
3. Compute guest-relative path by stripping `root_path` prefix from resolved path.
4. Store in `cwd`. Return `$00`.

### MKDIR — `"MD,<path>"`

`mkdir(resolved, 0755)`. EEXIST → `$02`, ENOENT → `$03`, else `$08` (permission).

### RMDIR — `"RD,<path>"`

`stat()` to verify it's a directory (`$0C` if not). `rmdir()`. ENOTEMPTY → `$0D`.

### REMOVE — `"RM,<path>"`

`unlink(resolved)`. ENOENT → `$01`, EISDIR → `$0C`.

### MOVE — `"MV,<src>,<dst>"`

1. Parse two comma-separated paths. Resolve both.
2. Self-move check (resolved src == resolved dst → `$0A`).
3. `stat(src)` → error `$01` if not found.
4. If dst is an existing directory, append `basename(src)` to dst.
5. `rename(resolved_src, resolved_dst)`.

### Helper: `parse_path_arg()`

Shared by CD, MD, RD, RM. Parses comma + path from args, validates length,
resolves via `storage_resolve_path()`. Sets appropriate command errors on failure.

## Tick Function

```cpp
void storage_tick() {
    // No-op in synchronous refill model.
    // Host filesystem reads complete instantly — buffer refill happens
    // inside storage_decode() when DISK_DATA is read.
    //
    // Future async backing stores would use tick to:
    //   - Check if pending async reads have completed
    //   - Fill channel read buffers from completed reads
    //   - Clear Busy flag in DISK_STATUS
}
```

Must still be called from `emulator_step()` to maintain the device pattern.

## Init and Reset

### storage_init()

```
memset(channels, 0, sizeof(channels));
current_channel = N8_DISK_CONTROL_CHAN;
cmd_len = 0;
cmd_overflow = false;
cmd_in_payload = false;
cmd_error_flag = false;
cmd_result_valid = false;
ctrl_io_error = 0;
strcpy(cwd, "/");
if (root_path[0] == '\0')
    strcpy(root_path, "./storage/");
```

### storage_reset()

Same as init, but does not reset `root_path` (that's configuration, not
device state). Close all open file handles first:

```
for (int i = 0; i < STORAGE_MAX_CHANNELS; i++) {
    if (channels[i].active)
        storage_close_channel(i);
}
// Then zero everything as in init
```

## Response Channel Lifecycle

Response channels (LIST, PWDIR) differ from file channels:

1. Created by command execution. Entire response materialized in memory.
2. Firmware reads via DISK_DATA. Buffer refill copies from `response_data`.
3. EOF set when `response_pos >= response_len` and buffer drains.
4. **Auto-close**: after the last byte is read from DISK_DATA (buf_count
   transitions to 0 with eof set), channel is closed immediately.
   `DISK_CHAN` bit 5 (Active) reads 0 for that channel afterward.
5. **Abort**: DISK_CTRL bit 0 (parser reset) closes all response channels.
   Firmware can abort a LIST mid-stream this way.

## Buffer Refill Strategy

Synchronous — all refills happen inside `storage_decode()` when firmware
reads DISK_DATA. Rationale:

- Host filesystem `fread()` completes instantly at emulation timescale.
- Busy (DISK_STATUS bit 7) is architecturally valid but never set to 1.
- The `bytes_avail == 0, Busy == 1, EOF == 0` state is unreachable.

The observable states from firmware's perspective collapse to:

| bytes_avail | EOF | Meaning                    |
|-------------|-----|----------------------------|
| > 0         | 0   | Data ready, more to come   |
| > 0         | 1   | Last bytes in buffer       |
| 0           | 1   | End of file                |

Eager refill after each byte read keeps `bytes_avail` in DISK_STATUS
accurate without requiring firmware to trigger refills explicitly.

## GDB Bridge

Add to `gdb_read_mem()` in `src/gdb_bridge.cpp`, after the video register block:

```cpp
if (addr >= N8_STORAGE_BASE && addr < N8_STORAGE_BASE + N8_DISK_REG_COUNT) {
    switch (addr - N8_STORAGE_BASE) {
        case N8_DISK_CHAN:   return storage_get_chan();
        case N8_DISK_DATA:   return 0x00;   // side-effect, don't trigger
        case N8_DISK_ERROR:  return storage_get_error();
        case N8_DISK_CTRL:   return 0x00;   // write-only
        case N8_DISK_STATUS: return storage_get_status();
        default:             return 0x00;
    }
}
```

DISK_DATA returns 0x00 via GDB to avoid buffer consumption and auto-close
side effects. Same rationale as VID_DATA.

Add `storage_reset()` to `gdb_reset()`.

## Emulator Integration

### emulator.cpp

```cpp
#include "emu_storage.h"

// In emulator_init(), after kbd_init():
storage_init();

// In emulator_reset(), after kbd_reset():
storage_reset();

// In emulator_step(), after tty_tick(pins):
storage_tick();

// In device switch, after case N8_KBD_SLOT:
case N8_STORAGE_SLOT:
    storage_decode(pins, reg);
    break;
```

### main.cpp

Change signature from `int main(int, char**)` to `int main(int argc, char** argv)`.

Before `emulator_init()`, parse CLI args:
```
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--storage") == 0 && i + 1 < argc) {
        storage_set_root_path(argv[++i]);
    }
}
```

`storage_set_root_path()` copies the path into `root_path` and ensures
trailing `/`. Create the directory if it doesn't exist (`mkdir -p` equivalent).

Default: `./storage/` relative to CWD (repo root).

### Makefile

```makefile
# Line 22, append:
SOURCES += $(SRC_DIR)/emu_storage.cpp

# Line 118-122, add to TEST_SRC_OBJS:
$(BUILD_DIR)/emu_storage.o \
```

## Edge Cases — Implementation Map

| Spec Edge Case | Implementation |
|---|---|
| Partial command + CTRL reset | CTRL bit 0: zero cmd_len/overflow/payload. Close all CHAN_RESPONSE. Clear errors. |
| Double null | Parser: byte == $00 && cmd_len == 0 → return (ignore). |
| Read on closed channel | DISK_DATA read: `!ch.active` → set io_error $01, return $00. |
| Write on read-only channel | DISK_DATA write on data channel with MODE_READ → io_error $04. |
| Command buffer overflow | 257th byte → cmd_overflow = true, ctrl_io_error = $06, reset parser. |
| Response channel auto-close | After read drains buf_count to 0 with eof: close if CHAN_RESPONSE. |
| DISK_CTRL bit 1 on closed channel | Check active. If false, set io_error $01. |
| DISK_STATUS on inactive channel | Return $00. |
| SK null in payload | cmd_in_payload == true: append all bytes including $00, count down. chan_id=0 is valid. |
| Double open | storage_check_already_open() before fopen. Error $02. |
| Move to self | `strcmp(resolved_src, resolved_dst) == 0` → error $0A. |
| Seek past EOF | `fseek` past end extends file with zeros (POSIX behavior). |
| Read on write channel | DISK_DATA read: `ch.mode != MODE_READ` → io_error $04. |
| Seek on append | `ch.mode == MODE_APPEND` → error $0A. |
| SEEK buffer invalidation | Read channels: compute logical pos, fseek absolute, reset buf, refill. |
| Move into directory | If dst is existing dir, append basename(src) to dst path. |

## Testing Strategy

### Unit Tests (test/test_storage.cpp)

Use `EmulatorFixture` for bus decode tests. Create a temp directory with
known files in test setup, clean up in teardown (`mkdtemp()` + populate).

**Test cases (Phase 1 + Phase 2):**

| Test | Description |
|------|-------------|
| Register basics | Write/read DISK_CHAN, verify channel select + active bit |
| DISK_CHAN clears error | Set error, write CHAN, verify cleared |
| DISK_CTRL bit 7 | Open channels, device reset, verify all closed |
| DISK_CTRL bit 0 | Partial command, parser reset, verify clean state |
| DISK_CTRL bit 1 | Open file, reset channel, verify closed |
| DISK_CTRL bit 1 on closed | Verify DISK_ERROR $01 |
| Phantom regs | Read reg 0x05-0x1F, all return $00 |
| PWDIR | Send "PD", read channel, verify "/" |
| LIST | Create test files, send "LS", read entries, verify format |
| OPEN R | Create file, send "OP,R,file", read content, verify match |
| OPEN W | Create new file, truncate existing file |
| OPEN A | Append to existing, create new if missing |
| CLOSE | Open, close, verify channel inactive |
| SEEK | Absolute, relative forward/backward, buffer invalidation, write channel |
| CHDIR | Change dir + PWDIR verify, affects relative paths, error on file/missing |
| MKDIR | Create directory, verify with LS, error on existing |
| RMDIR | Remove empty dir, error on non-empty/not-a-dir |
| REMOVE | Delete file, error on nonexistent |
| MOVE | Rename, move into directory, self-move error |
| Double null | Send "PD\0\0", verify single execution |
| Buffer overflow | Write 257 bytes, verify DISK_ERROR $06 |
| Read past EOF | Read entire file + 1, verify DISK_ERROR $03 |
| Read/write permission | Read on write channel, write on read channel → $04 |
| Invalid command | Send "XX", verify command error $09 |

### GDB Playground Test (firmware/gdb_playground/test_storage.s)

6502 assembly program that exercises the storage device:

1. Select control channel ($0F → DISK_CHAN).
2. Send "PD" + $00, read result channel, read path into $0300.
3. Send "LS" + $00, read listing into $0400.
4. Send "OP,R,testfile" + $00, read file content into $0500.
5. Send "CL,<chan>" + $00, verify success.
6. Hit `done:` breakpoint for GDB inspection.

Store results at known RAM addresses. Verify via n8gdb:
```
n8gdb read 0300 20    # PWDIR result
n8gdb read 0400 80    # LS entries
n8gdb read 0500 40    # file content
```

## Implementation Order

### Phase 1 (complete)
1. `n8_memory_map.h` — all constants
2. `emu_storage.h/cpp` — skeleton, register decode, command parser
3. Integration: `emulator.cpp`, `gdb_bridge.cpp`, `main.cpp`, `Makefile`
4. Commands: PWDIR → OPEN R → LIST → CLOSE
5. Tests + firmware playground

### Phase 2 (complete)
1. OPEN W/A + DISK_DATA write path (decode_data_write)
2. SEEK (explicit chan_id in binary payload, buffer invalidation)
3. CD, MD, RD, RM (parse_path_arg helper)
4. MOVE (two-path parsing, dir-as-destination)
