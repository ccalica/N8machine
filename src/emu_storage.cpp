#include "emu_storage.h"
#include "n8_memory_map.h"
#include "m6502.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <climits>
#include <cerrno>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <string>

// ============================================================
// Data Structures
// ============================================================

#define STORAGE_MAX_CHANNELS  15     // 0x00-0x0E (0x0F = control)
#define STORAGE_READ_BUF_SIZE 256
#define STORAGE_CMD_BUF_SIZE  257    // 256 payload + null
#define STORAGE_PATH_MAX      1024
#define STORAGE_FILENAME_MAX  63

enum channel_type_t {
    CHAN_NONE = 0,
    CHAN_FILE,
    CHAN_RESPONSE
};

enum channel_mode_t {
    MODE_NONE = 0,
    MODE_READ,
    MODE_WRITE,
    MODE_APPEND
};

struct channel_t {
    channel_type_t type;
    channel_mode_t mode;
    bool           active;

    // File channel (CHAN_FILE)
    FILE*          fp;
    char           host_path[STORAGE_PATH_MAX];

    // Response channel (CHAN_RESPONSE)
    uint8_t*       response_data;
    int            response_len;
    int            response_pos;

    // Read buffer (both channel types)
    uint8_t        read_buf[STORAGE_READ_BUF_SIZE];
    int            buf_pos;
    int            buf_count;
    bool           eof;

    // Per-channel I/O error
    uint8_t        io_error;
};

// ============================================================
// Device State
// ============================================================

static channel_t channels[STORAGE_MAX_CHANNELS];
static uint8_t   current_channel;

// Command parser
static uint8_t   cmd_buf[STORAGE_CMD_BUF_SIZE];
static int       cmd_len;
static bool      cmd_overflow;
static bool      cmd_in_payload;
static int       cmd_expected_payload;

// Command result
static bool      cmd_error_flag;
static uint8_t   cmd_error_code;
static bool      cmd_result_valid;
static uint8_t   cmd_result;

// Control channel I/O error
static uint8_t   ctrl_io_error;

// Filesystem
static char      root_path[STORAGE_PATH_MAX];
static char      cwd[STORAGE_PATH_MAX];

// ============================================================
// Forward declarations
// ============================================================

static int  storage_alloc_channel();
static void storage_close_channel(int id);
static bool storage_resolve_path(const char* guest_path, char* out, size_t out_size);
static void storage_refill_buffer(int chan_id);
static bool storage_check_already_open(const char* resolved_path);
static void storage_execute_command();
static void parser_feed_byte(uint8_t byte);

// Command handlers
static void cmd_list(const char* args);
static void cmd_open(const char* args);
static void cmd_close(const char* args);
static void cmd_pwdir(const char* args);
static void cmd_seek(const char* args);
static void cmd_chdir(const char* args);
static void cmd_mkdir(const char* args);
static void cmd_rmdir(const char* args);
static void cmd_remove(const char* args);
static void cmd_move(const char* args);

// ============================================================
// Init / Reset / Tick
// ============================================================

void storage_init() {
    memset(channels, 0, sizeof(channels));
    current_channel = N8_DISK_CONTROL_CHAN;
    cmd_len = 0;
    cmd_overflow = false;
    cmd_in_payload = false;
    cmd_expected_payload = 0;
    cmd_error_flag = false;
    cmd_error_code = 0;
    cmd_result_valid = false;
    cmd_result = 0;
    ctrl_io_error = 0;
    strcpy(cwd, "/");
    if (root_path[0] == '\0')
        strcpy(root_path, "./storage/");
}

void storage_reset() {
    for (int i = 0; i < STORAGE_MAX_CHANNELS; i++) {
        if (channels[i].active)
            storage_close_channel(i);
    }
    memset(channels, 0, sizeof(channels));
    current_channel = N8_DISK_CONTROL_CHAN;
    cmd_len = 0;
    cmd_overflow = false;
    cmd_in_payload = false;
    cmd_expected_payload = 0;
    cmd_error_flag = false;
    cmd_error_code = 0;
    cmd_result_valid = false;
    cmd_result = 0;
    ctrl_io_error = 0;
    strcpy(cwd, "/");
}

void storage_tick() {
    // No-op: synchronous refill model (host FS is instant)
}

// ============================================================
// Configuration
// ============================================================

void storage_set_root_path(const char* path) {
    strncpy(root_path, path, STORAGE_PATH_MAX - 2);
    root_path[STORAGE_PATH_MAX - 2] = '\0';
    // Ensure trailing /
    size_t len = strlen(root_path);
    if (len > 0 && root_path[len - 1] != '/') {
        root_path[len] = '/';
        root_path[len + 1] = '\0';
    }
}

// ============================================================
// Register Decode
//
// Called once per bus cycle when addr falls in slot 4 ($D880-$D89F).
// The 6502 bus places an address and R/W signal; we decode the
// register offset and either put data on the bus (read) or
// consume data from the bus (write).
// ============================================================

// Forward declaration for DISK_DATA helpers
static void decode_data_read(uint64_t& pins);
static void decode_data_write(uint8_t val);

// Hardware register decode — called from device router on every bus cycle
// targeting $D880-$D884.
//
// Data flow overview:
//   CPU → DISK_CHAN (W)  → selects active channel, clears error state
//   CPU → DISK_DATA (W)  → control chan: feeds command parser byte-by-byte
//                           data chan: writes byte to host file via fwrite()
//   CPU ← DISK_DATA (R)  → control chan: returns cmd error code or result
//                           data chan: returns next byte from 256B read buffer
//   CPU ← DISK_ERROR (R) → control chan: cmd error flag (bit 7) | I/O error
//                           data chan: per-channel I/O error latch
//   CPU → DISK_CTRL (W)  → edge-triggered: device reset / parser reset / close
//   CPU ← DISK_STATUS (R)→ buffer fill (bits 0-3), EOF, CTS, Busy flags
//
// Firmware protocol: select channel via CHAN, then read/write DATA.
// Commands are sent as ASCII opcodes + args to the control channel (0x0F),
// terminated by null byte. Binary-payload opcodes (SK, CL) use the parser's
// payload mode to avoid null-terminator ambiguity.
void storage_decode(uint64_t& pins, uint8_t reg) {
    bool is_read = (pins & M6502_RW);
    uint8_t val = is_read ? 0 : M6502_GET_DATA(pins);

    switch (reg) {

    // ---- DISK_CHAN ($D880) ----
    // Write: select channel (lo nibble). Clears error state for that channel.
    // Read:  returns current channel ID + active bit (bit 5).
    case N8_DISK_CHAN: {
        if (is_read) {
            uint8_t result = current_channel & N8_DISK_CHAN_MASK;
            if (current_channel < STORAGE_MAX_CHANNELS && channels[current_channel].active)
                result |= N8_DISK_CHAN_ACTIVE;
            M6502_SET_DATA(pins, result);
        } else {
            uint8_t id = val & N8_DISK_CHAN_MASK;
            current_channel = id;
            // Selecting a channel clears its error state
            if (id == N8_DISK_CONTROL_CHAN) {
                ctrl_io_error = 0;
                cmd_error_flag = false;
                cmd_error_code = 0;
            } else if (id < STORAGE_MAX_CHANNELS) {
                channels[id].io_error = 0;
            }
        }
        break;
    }

    // ---- DISK_DATA ($D881) ----
    // Write on control channel: feeds byte into command parser.
    // Write on data channel:    writes to file (Phase 2; currently errors).
    // Read on control channel:  returns command error code or result byte.
    // Read on data channel:     returns next byte from read buffer, refills as needed.
    case N8_DISK_DATA: {
        if (is_read)
            decode_data_read(pins);
        else
            decode_data_write(val);
        break;
    }

    // ---- DISK_ERROR ($D882) ----
    // Read-only. Returns I/O error for data channels, or command error flag
    // (bit 7) for the control channel. Latches until cleared by DISK_CHAN write.
    case N8_DISK_ERROR: {
        if (is_read) {
            if (current_channel == N8_DISK_CONTROL_CHAN) {
                uint8_t err = ctrl_io_error;
                if (cmd_error_flag) err = N8_DISK_ERR_CMD;
                M6502_SET_DATA(pins, err);
            } else if (current_channel < STORAGE_MAX_CHANNELS) {
                M6502_SET_DATA(pins, channels[current_channel].io_error);
            } else {
                M6502_SET_DATA(pins, 0x00);
            }
        }
        // Writes ignored (read-only register)
        break;
    }

    // ---- DISK_CTRL ($D883) ----
    // Write-only, edge-triggered. Bit 7: full device reset. Bit 0: reset
    // command parser + close response channels. Bit 1: close current channel.
    case N8_DISK_CTRL: {
        if (is_read) {
            M6502_SET_DATA(pins, 0x00);
        } else {
            // Bit 7: full device reset (close all, clear all state)
            if (val & N8_DISK_CTRL_DEVICE) {
                storage_reset();
                return;
            }
            // Bit 0: reset command parser, close response channels
            if (val & N8_DISK_CTRL_PARSER) {
                cmd_len = 0;
                cmd_overflow = false;
                cmd_in_payload = false;
                cmd_error_flag = false;
                cmd_error_code = 0;
                cmd_result_valid = false;
                ctrl_io_error = 0;
                for (int i = 0; i < STORAGE_MAX_CHANNELS; i++) {
                    if (channels[i].type == CHAN_RESPONSE && channels[i].active)
                        storage_close_channel(i);
                }
            }
            // Bit 1: close the currently selected channel
            if (val & N8_DISK_CTRL_CHANNEL) {
                if (current_channel >= STORAGE_MAX_CHANNELS ||
                    !channels[current_channel].active) {
                    if (current_channel < STORAGE_MAX_CHANNELS)
                        channels[current_channel].io_error = N8_DISK_IOE_NOT_OPEN;
                } else {
                    storage_close_channel(current_channel);
                }
            }
        }
        break;
    }

    // ---- DISK_STATUS ($D884) ----
    // Read-only. Returns buffer fill level (bits 0-3, saturates at 15),
    // EOF (bit 5), CTS (bit 6, always set), Busy (bit 7).
    // Response channels auto-close when firmware reads STATUS showing EOF.
    case N8_DISK_STATUS: {
        if (is_read) {
            if (current_channel >= STORAGE_MAX_CHANNELS ||
                !channels[current_channel].active) {
                M6502_SET_DATA(pins, 0x00);
            } else {
                channel_t& ch = channels[current_channel];
                uint8_t status = 0;
                int avail = ch.buf_count;
                if (avail > 15) avail = 15;
                status |= (avail & N8_DISK_STAT_AVAIL);
                if (ch.eof && ch.buf_count == 0)
                    status |= N8_DISK_STAT_EOF;
                status |= N8_DISK_STAT_CTS;
                if (ch.buf_count == 0 && !ch.eof)
                    status |= N8_DISK_STAT_BUSY;
                M6502_SET_DATA(pins, status);
                // Auto-close response channels when firmware observes EOF
                if (ch.eof && ch.buf_count == 0 && ch.type == CHAN_RESPONSE)
                    storage_close_channel(current_channel);
            }
        }
        // Writes ignored (read-only register)
        break;
    }

    default:
        // Phantom registers beyond REG_COUNT: read $00, write ignored
        if (is_read) M6502_SET_DATA(pins, 0x00);
        break;
    }
}

// ---- DISK_DATA read helper ----
// Control channel: return command error code or result byte.
// Data channel: return next byte from the 256-byte read-ahead buffer.
//   Refills buffer from host file/response when drained.
static void decode_data_read(uint64_t& pins) {
    if (current_channel == N8_DISK_CONTROL_CHAN) {
        // Command error code takes priority, then result byte, then $00
        if (cmd_error_flag) {
            M6502_SET_DATA(pins, cmd_error_code);
        } else if (cmd_result_valid) {
            M6502_SET_DATA(pins, cmd_result);
            cmd_result_valid = false;  // consumed
        } else {
            M6502_SET_DATA(pins, 0x00);
        }
        return;
    }
    if (current_channel >= STORAGE_MAX_CHANNELS) {
        M6502_SET_DATA(pins, 0x00);
        return;
    }
    channel_t& ch = channels[current_channel];
    if (!ch.active) {
        ch.io_error = N8_DISK_IOE_NOT_OPEN;
        M6502_SET_DATA(pins, 0x00);
    } else if (ch.mode != MODE_READ) {
        ch.io_error = N8_DISK_IOE_PERMISSION;
        M6502_SET_DATA(pins, 0x00);
    } else if (ch.buf_count == 0 && ch.eof) {
        ch.io_error = N8_DISK_IOE_PAST_EOF;
        M6502_SET_DATA(pins, 0x00);
    } else {
        // Refill if buffer empty but stream not finished
        if (ch.buf_count == 0 && !ch.eof)
            storage_refill_buffer(current_channel);
        if (ch.buf_count > 0) {
            uint8_t byte = ch.read_buf[ch.buf_pos++];
            ch.buf_count--;
            M6502_SET_DATA(pins, byte);
            // Eager refill keeps bytes_avail in STATUS accurate
            if (ch.buf_count == 0 && !ch.eof)
                storage_refill_buffer(current_channel);
        } else {
            M6502_SET_DATA(pins, 0x00);
        }
    }
}

// ---- DISK_DATA write helper ----
// Control channel: feed byte into command parser (accumulate, null = execute).
// Data channel: write to file channel (Phase 2; currently errors on read-only).
static void decode_data_write(uint8_t val) {
    if (current_channel == N8_DISK_CONTROL_CHAN) {
        parser_feed_byte(val);
    } else if (current_channel < STORAGE_MAX_CHANNELS) {
        channel_t& ch = channels[current_channel];
        if (!ch.active) {
            ch.io_error = N8_DISK_IOE_NOT_OPEN;
        } else if (ch.mode == MODE_READ) {
            ch.io_error = N8_DISK_IOE_PERMISSION;
        } else if (ch.mode == MODE_WRITE || ch.mode == MODE_APPEND) {
            if (fwrite(&val, 1, 1, ch.fp) != 1) {
                ch.io_error = N8_DISK_IOE_DISK_FULL;
            }
        }
    }
}

// ============================================================
// Command Parser
// ============================================================

// Check if the accumulated command buffer matches a binary-payload opcode.
// Called after each byte appended. Some commands (e.g., SK) have fixed-length
// binary payloads where $00 is valid data, not a null terminator. When a
// binary opcode is detected, the parser switches to payload mode and counts
// bytes instead of scanning for null.
// Currently SK and CL have binary payloads.
// SK: ,<chan_id>,<type>,<lo><hi> = 6 bytes after the opcode prefix comma.
// CL: <chan_id> = 1 byte after first comma.
// Extend this function when adding future binary-payload commands.
static void parser_check_binary_opcode() {
    if (cmd_len == 3 && cmd_buf[2] == ',') {
        if (cmd_buf[0] == 'S' && cmd_buf[1] == 'K') {
            cmd_in_payload = true;
            cmd_expected_payload = 6;  // chan_id + ',' + type + ',' + lo + hi
        } else if (cmd_buf[0] == 'C' && cmd_buf[1] == 'L') {
            cmd_in_payload = true;
            cmd_expected_payload = 1;  // channel ID byte
        }
    }
}

static void parser_feed_byte(uint8_t byte) {
    if (cmd_overflow) return;

    if (cmd_in_payload) {
        cmd_buf[cmd_len++] = byte;
        cmd_expected_payload--;
        if (cmd_expected_payload == 0)
            cmd_in_payload = false;
        if (cmd_len > 256) {
            cmd_overflow = true;
            ctrl_io_error = N8_DISK_IOE_CMD_OVERFLOW;
            cmd_len = 0;
            cmd_in_payload = false;
        }
        return;
    }

    if (byte == 0x00) {
        if (cmd_len == 0) return;  // double null / idle
        cmd_buf[cmd_len] = 0;
        storage_execute_command();
        return;
    }

    cmd_buf[cmd_len++] = byte;
    if (cmd_len > 256) {
        cmd_overflow = true;
        ctrl_io_error = N8_DISK_IOE_CMD_OVERFLOW;
        cmd_len = 0;
        cmd_in_payload = false;
        return;
    }

    parser_check_binary_opcode();
}

static void storage_execute_command() {
    int saved_len = cmd_len;
    cmd_len = 0;
    cmd_in_payload = false;
    cmd_overflow = false;

    cmd_error_flag = false;
    cmd_error_code = 0;
    cmd_result_valid = false;
    cmd_result = 0;

    if (saved_len < 2) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    char op0 = cmd_buf[0], op1 = cmd_buf[1];
    const char* args = (const char*)&cmd_buf[2];

    if      (op0 == 'L' && op1 == 'S') cmd_list(args);
    else if (op0 == 'O' && op1 == 'P') cmd_open(args);
    else if (op0 == 'C' && op1 == 'L') cmd_close(args);
    else if (op0 == 'P' && op1 == 'D') cmd_pwdir(args);
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

// ============================================================
// Helper Functions
// ============================================================

static int storage_alloc_channel() {
    for (int i = 0; i < STORAGE_MAX_CHANNELS; i++) {
        if (!channels[i].active)
            return i;
    }
    return -1;
}

static void storage_close_channel(int id) {
    if (id < 0 || id >= STORAGE_MAX_CHANNELS) return;
    if (channels[id].type == CHAN_FILE && channels[id].fp) {
        fclose(channels[id].fp);
    }
    if (channels[id].type == CHAN_RESPONSE && channels[id].response_data) {
        free(channels[id].response_data);
    }
    memset(&channels[id], 0, sizeof(channel_t));
}

static bool storage_resolve_path(const char* guest_path, char* out, size_t out_size) {
    char joined[STORAGE_PATH_MAX];

    if (guest_path[0] == '/') {
        // Absolute path
        snprintf(joined, sizeof(joined), "%s%s", root_path, guest_path + 1);
    } else {
        // Relative to cwd
        if (strcmp(cwd, "/") == 0) {
            snprintf(joined, sizeof(joined), "%s%s", root_path, guest_path);
        } else {
            snprintf(joined, sizeof(joined), "%s%s/%s", root_path, cwd + 1, guest_path);
        }
    }

    // Validate each path component: reject ".." traversal and names > FILENAME_MAX
    const char* p = guest_path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
            return false;
        }
        // Measure component length
        const char* comp_start = p;
        while (*p && *p != '/') p++;
        size_t comp_len = (size_t)(p - comp_start);
        if (comp_len > STORAGE_FILENAME_MAX) {
            return false;
        }
        while (*p == '/') p++;
    }

    // Resolve to real path and verify it's under root
    char resolved[PATH_MAX];
    char resolved_root[PATH_MAX];

    // Resolve root_path first
    if (!realpath(root_path, resolved_root)) {
        return false;
    }
    size_t root_len = strlen(resolved_root);

    // Try to resolve the full path. If the file doesn't exist yet,
    // resolve the parent directory instead (for OPEN write mode later).
    if (realpath(joined, resolved)) {
        // Verify prefix
        if (strncmp(resolved, resolved_root, root_len) != 0) {
            return false;
        }
        // Must be root itself or have / after root prefix
        if (resolved[root_len] != '\0' && resolved[root_len] != '/') {
            return false;
        }
        strncpy(out, resolved, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    // Path doesn't exist — resolve parent for validation
    char parent[STORAGE_PATH_MAX];
    strncpy(parent, joined, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    // Find last /
    char* last_slash = strrchr(parent, '/');
    if (!last_slash) return false;
    *last_slash = '\0';

    if (!realpath(parent, resolved)) {
        return false;
    }
    if (strncmp(resolved, resolved_root, root_len) != 0) {
        return false;
    }
    if (resolved[root_len] != '\0' && resolved[root_len] != '/') {
        return false;
    }

    // Reconstruct: resolved parent + / + filename
    snprintf(out, out_size, "%s/%s", resolved, last_slash + 1);
    return true;
}

// Refill the 256-byte read-ahead buffer for a channel.
//
// Hardware data flow:
//   DISK_DATA read → decode_data_read() consumes buf[buf_pos++], decrements buf_count
//   → when buf_count hits 0, this function refills from the backing source
//   → DISK_STATUS reports buf_count in bits 0-3 (saturates at 15)
//
// Two backing sources:
//   CHAN_FILE:     host FILE* — fread() up to 256 bytes. Short read sets EOF.
//   CHAN_RESPONSE: in-memory buffer (LIST/PWDIR result) — memcpy next chunk.
//
// Called eagerly after each DISK_DATA read so STATUS.avail stays accurate.
// Also called after SEEK to prime the buffer at the new file position.
static void storage_refill_buffer(int chan_id) {
    channel_t& ch = channels[chan_id];
    if (ch.eof) return;

    if (ch.type == CHAN_FILE) {
        size_t n = fread(ch.read_buf, 1, STORAGE_READ_BUF_SIZE, ch.fp);
        ch.buf_pos = 0;
        ch.buf_count = (int)n;
        if ((int)n < STORAGE_READ_BUF_SIZE)
            ch.eof = true;  // short read = end of file
    } else if (ch.type == CHAN_RESPONSE) {
        int remaining = ch.response_len - ch.response_pos;
        int to_copy = (remaining < STORAGE_READ_BUF_SIZE) ? remaining : STORAGE_READ_BUF_SIZE;
        memcpy(ch.read_buf, ch.response_data + ch.response_pos, to_copy);
        ch.response_pos += to_copy;
        ch.buf_pos = 0;
        ch.buf_count = to_copy;
        if (ch.response_pos >= ch.response_len)
            ch.eof = true;  // entire response consumed
    }
}

static bool storage_check_already_open(const char* resolved_path) {
    for (int i = 0; i < STORAGE_MAX_CHANNELS; i++) {
        if (channels[i].active && channels[i].type == CHAN_FILE) {
            if (strcmp(channels[i].host_path, resolved_path) == 0)
                return true;
        }
    }
    return false;
}

// ============================================================
// Command Implementations
// ============================================================

static void cmd_pwdir(const char* args) {
    // "PD" — no args expected
    if (args[0] != '\0') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    int ch_id = storage_alloc_channel();
    if (ch_id < 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NO_FREE_CHAN;
        return;
    }

    int len = (int)strlen(cwd);
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DISK_FULL;
        return;
    }
    channel_t& ch = channels[ch_id];
    ch.type = CHAN_RESPONSE;
    ch.mode = MODE_READ;
    ch.active = true;
    ch.response_data = buf;
    memcpy(ch.response_data, cwd, len);
    ch.response_len = len;
    ch.response_pos = 0;

    storage_refill_buffer(ch_id);

    cmd_result = (uint8_t)ch_id;
    cmd_result_valid = true;
}

static void cmd_list(const char* args) {
    // "LS,<path>" or "LS"
    const char* path = ".";
    if (args[0] == ',') {
        path = args + 1;
        if (path[0] == '\0') path = ".";
    } else if (args[0] != '\0') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    char resolved[STORAGE_PATH_MAX];
    if (!storage_resolve_path(path, resolved, sizeof(resolved))) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
        return;
    }

    // Check if it's a directory
    struct stat st;
    if (stat(resolved, &st) != 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_FILE_NOT_FOUND;
        return;
    }

    // Build response
    std::vector<uint8_t> response;

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(resolved);
        if (!dir) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
            return;
        }

        // Collect entries
        struct ls_entry {
            bool is_dir;
            std::string name;
            uint16_t size;
        };
        std::vector<ls_entry> entries;

        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            char full_path[STORAGE_PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", resolved, ent->d_name);

            struct stat entry_stat;
            if (stat(full_path, &entry_stat) != 0)
                continue;

            ls_entry e;
            e.is_dir = S_ISDIR(entry_stat.st_mode);
            e.name = ent->d_name;
            if (e.is_dir) {
                e.size = 0;
            } else {
                e.size = (entry_stat.st_size > 0xFFFF) ? 0xFFFF : (uint16_t)entry_stat.st_size;
            }
            entries.push_back(e);
        }
        closedir(dir);

        // Sort: directories first, then files, alphabetical within each group
        std::sort(entries.begin(), entries.end(), [](const ls_entry& a, const ls_entry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return a.name < b.name;
        });

        // Format: D|F \t <name> \t <size_lo> <size_hi> \n
        for (size_t i = 0; i < entries.size(); i++) {
            response.push_back(entries[i].is_dir ? 'D' : 'F');
            response.push_back('\t');
            for (size_t j = 0; j < entries[i].name.size(); j++)
                response.push_back((uint8_t)entries[i].name[j]);
            response.push_back('\t');
            response.push_back((uint8_t)(entries[i].size & 0xFF));
            response.push_back((uint8_t)((entries[i].size >> 8) & 0xFF));
            response.push_back('\n');
        }
    } else {
        // Single file listing
        const char* basename = strrchr(resolved, '/');
        basename = basename ? basename + 1 : resolved;

        uint16_t size = (st.st_size > 0xFFFF) ? 0xFFFF : (uint16_t)st.st_size;
        response.push_back('F');
        response.push_back('\t');
        for (const char* p = basename; *p; p++)
            response.push_back((uint8_t)*p);
        response.push_back('\t');
        response.push_back((uint8_t)(size & 0xFF));
        response.push_back((uint8_t)((size >> 8) & 0xFF));
        response.push_back('\n');
    }

    int ch_id = storage_alloc_channel();
    if (ch_id < 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NO_FREE_CHAN;
        return;
    }

    int rlen = (int)response.size();
    uint8_t* rbuf = rlen > 0 ? (uint8_t*)malloc(rlen) : NULL;
    if (rlen > 0 && !rbuf) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DISK_FULL;
        return;
    }
    channel_t& ch = channels[ch_id];
    ch.type = CHAN_RESPONSE;
    ch.mode = MODE_READ;
    ch.active = true;
    ch.response_len = rlen;
    ch.response_data = rbuf;
    if (rlen > 0)
        memcpy(ch.response_data, response.data(), rlen);
    ch.response_pos = 0;

    storage_refill_buffer(ch_id);

    cmd_result = (uint8_t)ch_id;
    cmd_result_valid = true;
}

static void cmd_open(const char* args) {
    // "OP,<mode>,<filename>"  — args starts after "OP"
    if (args[0] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    char mode_char = args[1];
    if (args[2] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    channel_mode_t mode;
    const char* fmode;
    if (mode_char == 'R') {
        mode = MODE_READ;
        fmode = "rb";
    } else if (mode_char == 'W') {
        mode = MODE_WRITE;
        fmode = "wb";
    } else if (mode_char == 'A') {
        mode = MODE_APPEND;
        fmode = "ab";
    } else {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_ARG;
        return;
    }

    const char* filename = args + 3;
    if (filename[0] == '\0') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    if (strlen(filename) > STORAGE_FILENAME_MAX) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NAME_TOO_LONG;
        return;
    }

    char resolved[STORAGE_PATH_MAX];
    if (!storage_resolve_path(filename, resolved, sizeof(resolved))) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_FILE_NOT_FOUND;
        return;
    }

    if (storage_check_already_open(resolved)) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_FILE_EXISTS;
        return;
    }

    FILE* fp = fopen(resolved, fmode);
    if (!fp) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_FILE_NOT_FOUND;
        return;
    }

    int ch_id = storage_alloc_channel();
    if (ch_id < 0) {
        fclose(fp);
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NO_FREE_CHAN;
        return;
    }

    channel_t& ch = channels[ch_id];
    ch.type = CHAN_FILE;
    ch.mode = mode;
    ch.active = true;
    ch.fp = fp;
    strncpy(ch.host_path, resolved, STORAGE_PATH_MAX - 1);
    ch.host_path[STORAGE_PATH_MAX - 1] = '\0';

    if (mode == MODE_READ) {
        storage_refill_buffer(ch_id);
    }

    cmd_result = (uint8_t)ch_id;
    cmd_result_valid = true;
}

static void cmd_close(const char* args) {
    // "CL,<chan_id>" — chan_id is a single binary byte (0x00-0x0E), not ASCII.
    // Similar framing to SEEK: the parser knows CL has a 1-byte binary payload
    // after the comma. The null terminator follows the payload byte.
    if (args[0] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    int id = (uint8_t)args[1];

    if (id >= STORAGE_MAX_CHANNELS) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_ARG;
        return;
    }

    if (!channels[id].active) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_CHAN_NOT_OPEN;
        return;
    }

    storage_close_channel(id);
    cmd_result = 0x00;
    cmd_result_valid = true;
}

// ============================================================
// Phase 2 Commands: SEEK, CHDIR, MKDIR, RMDIR, REMOVE, MOVE
// ============================================================

static void cmd_seek(const char* args) {
    // "SK,<chan_id>,<type>,<lo><hi>" — binary payload (6 bytes after first comma)
    // Parser collects: S K , [chan_id , type , lo hi] — 3 text + 6 payload = 9 in cmd_buf
    // args = &cmd_buf[2], so args[0..6] = , chan_id , type , lo hi
    if (args[0] != ',' || args[2] != ',' || args[4] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    uint8_t target_chan = (uint8_t)args[1];
    uint8_t type       = (uint8_t)args[3];
    uint8_t lo         = (uint8_t)args[5];
    uint8_t hi         = (uint8_t)args[6];
    long offset        = (long)(lo | (hi << 8));

    if (target_chan >= STORAGE_MAX_CHANNELS) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_ARG;
        return;
    }
    channel_t& ch = channels[target_chan];
    if (!ch.active || ch.type != CHAN_FILE) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_CHAN_NOT_OPEN;
        return;
    }
    if (ch.mode == MODE_APPEND) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_ARG;
        return;
    }

    // For read channels, compute logical position (FILE* is ahead by buf_count)
    long target;
    if (ch.mode == MODE_READ) {
        long file_pos = ftell(ch.fp);
        long logical = file_pos - ch.buf_count;
        if (type == 'A')      target = offset;
        else if (type == '+') target = logical + offset;
        else if (type == '-') target = logical - offset;
        else {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_BAD_ARG;
            return;
        }
        if (target < 0) target = 0;
        if (fseek(ch.fp, target, SEEK_SET) != 0) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_BAD_ARG;
            return;
        }
        // Invalidate read buffer and refill
        ch.buf_pos = 0;
        ch.buf_count = 0;
        ch.eof = false;
        storage_refill_buffer(target_chan);
    } else {
        // Write channel — flush libc buffer so ftell/fseek are accurate
        if (fflush(ch.fp) != 0) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_DISK_FULL;
            return;
        }
        int seek_rc;
        if (type == 'A')      { seek_rc = fseek(ch.fp, offset, SEEK_SET); }
        else if (type == '+') { seek_rc = fseek(ch.fp, offset, SEEK_CUR); }
        else if (type == '-') { seek_rc = fseek(ch.fp, -offset, SEEK_CUR); }
        else {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_BAD_ARG;
            return;
        }
        if (seek_rc != 0) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_BAD_ARG;
            return;
        }
    }

    cmd_result = 0x00;
    cmd_result_valid = true;
}

// Helper: parse comma + path from args, resolve path
// Returns true on success with resolved path in out. Sets cmd_error on failure.
static bool parse_path_arg(const char* args, char* out, size_t out_size) {
    if (args[0] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return false;
    }
    const char* path = args + 1;
    if (path[0] == '\0') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return false;
    }
    if (!storage_resolve_path(path, out, out_size)) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
        return false;
    }
    return true;
}

static void cmd_chdir(const char* args) {
    char resolved[STORAGE_PATH_MAX];
    if (!parse_path_arg(args, resolved, sizeof(resolved)))
        return;

    struct stat st;
    if (stat(resolved, &st) != 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NOT_A_DIR;
        return;
    }

    // Compute guest-relative path by stripping root_path prefix
    char resolved_root[PATH_MAX];
    if (!realpath(root_path, resolved_root)) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NOT_READY;
        return;
    }
    size_t root_len = strlen(resolved_root);

    if (strncmp(resolved, resolved_root, root_len) == 0) {
        const char* suffix = resolved + root_len;
        if (suffix[0] == '/') {
            snprintf(cwd, sizeof(cwd), "%s", suffix);
        } else if (suffix[0] == '\0') {
            strcpy(cwd, "/");
        } else {
            snprintf(cwd, sizeof(cwd), "/%s", suffix);
        }
    } else {
        strcpy(cwd, "/");
    }

    cmd_result = 0x00;
    cmd_result_valid = true;
}

static void cmd_mkdir(const char* args) {
    char resolved[STORAGE_PATH_MAX];
    if (!parse_path_arg(args, resolved, sizeof(resolved)))
        return;

    if (mkdir(resolved, 0755) != 0) {
        if (errno == EEXIST) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_FILE_EXISTS;
        } else if (errno == ENOENT) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
        } else {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_PERMISSION;
        }
        return;
    }

    cmd_result = 0x00;
    cmd_result_valid = true;
}

static void cmd_rmdir(const char* args) {
    char resolved[STORAGE_PATH_MAX];
    if (!parse_path_arg(args, resolved, sizeof(resolved)))
        return;

    struct stat st;
    if (stat(resolved, &st) != 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NOT_A_DIR;
        return;
    }

    if (rmdir(resolved) != 0) {
        if (errno == ENOTEMPTY) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_DIR_NOT_EMPTY;
        } else {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_PERMISSION;
        }
        return;
    }

    cmd_result = 0x00;
    cmd_result_valid = true;
}

static void cmd_remove(const char* args) {
    char resolved[STORAGE_PATH_MAX];
    if (!parse_path_arg(args, resolved, sizeof(resolved)))
        return;

    if (unlink(resolved) != 0) {
        if (errno == ENOENT) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_FILE_NOT_FOUND;
        } else if (errno == EISDIR) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_IS_A_DIR;
        } else {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_PERMISSION;
        }
        return;
    }

    cmd_result = 0x00;
    cmd_result_valid = true;
}

static void cmd_move(const char* args) {
    // "MV,<src>,<dst>"
    if (args[0] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    // Find second comma to split src and dst
    const char* src_start = args + 1;
    const char* comma2 = strchr(src_start, ',');
    if (!comma2 || comma2 == src_start) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }
    const char* dst_start = comma2 + 1;
    if (dst_start[0] == '\0') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    // Extract src as a separate string
    char src_name[STORAGE_FILENAME_MAX + 1];
    size_t src_len = (size_t)(comma2 - src_start);
    if (src_len == 0 || src_len > STORAGE_FILENAME_MAX) {
        cmd_error_flag = true;
        cmd_error_code = (src_len == 0) ? N8_DISK_CE_BAD_SYNTAX : N8_DISK_CE_NAME_TOO_LONG;
        return;
    }
    memcpy(src_name, src_start, src_len);
    src_name[src_len] = '\0';

    if (strlen(dst_start) > STORAGE_FILENAME_MAX) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_NAME_TOO_LONG;
        return;
    }

    char resolved_src[STORAGE_PATH_MAX];
    if (!storage_resolve_path(src_name, resolved_src, sizeof(resolved_src))) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_FILE_NOT_FOUND;
        return;
    }

    char resolved_dst[STORAGE_PATH_MAX];
    if (!storage_resolve_path(dst_start, resolved_dst, sizeof(resolved_dst))) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_DIR_NOT_FOUND;
        return;
    }

    // Self-move check
    if (strcmp(resolved_src, resolved_dst) == 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_ARG;
        return;
    }

    // Check if source exists
    struct stat src_st;
    if (stat(resolved_src, &src_st) != 0) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_FILE_NOT_FOUND;
        return;
    }

    // If dst is an existing directory, move src into it
    struct stat dst_st;
    if (stat(resolved_dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        // Extract basename from src
        const char* base = strrchr(resolved_src, '/');
        base = base ? base + 1 : resolved_src;
        char final_dst[STORAGE_PATH_MAX];
        snprintf(final_dst, sizeof(final_dst), "%s/%s", resolved_dst, base);
        if (rename(resolved_src, final_dst) != 0) {
            cmd_error_flag = true;
            cmd_error_code = N8_DISK_CE_PERMISSION;
            return;
        }
    } else {
        if (rename(resolved_src, resolved_dst) != 0) {
            cmd_error_flag = true;
            cmd_error_code = (errno == ENOENT) ? N8_DISK_CE_FILE_NOT_FOUND : N8_DISK_CE_PERMISSION;
            return;
        }
    }

    cmd_result = 0x00;
    cmd_result_valid = true;
}

// ============================================================
// GDB Bridge Accessors
// ============================================================

uint8_t storage_get_chan() {
    uint8_t result = current_channel & N8_DISK_CHAN_MASK;
    if (current_channel < STORAGE_MAX_CHANNELS && channels[current_channel].active)
        result |= N8_DISK_CHAN_ACTIVE;
    return result;
}

uint8_t storage_get_error() {
    if (current_channel == N8_DISK_CONTROL_CHAN) {
        if (cmd_error_flag) return N8_DISK_ERR_CMD;
        return ctrl_io_error;
    }
    if (current_channel < STORAGE_MAX_CHANNELS)
        return channels[current_channel].io_error;
    return 0x00;
}

uint8_t storage_get_status() {
    if (current_channel >= STORAGE_MAX_CHANNELS ||
        !channels[current_channel].active)
        return 0x00;

    channel_t& ch = channels[current_channel];
    uint8_t status = 0;
    int avail = ch.buf_count;
    if (avail > 15) avail = 15;
    status |= (avail & N8_DISK_STAT_AVAIL);
    if (ch.eof && ch.buf_count == 0)
        status |= N8_DISK_STAT_EOF;
    status |= N8_DISK_STAT_CTS;
    if (ch.buf_count == 0 && !ch.eof)
        status |= N8_DISK_STAT_BUSY;
    return status;
}
