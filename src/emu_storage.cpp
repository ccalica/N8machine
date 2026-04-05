#include "emu_storage.h"
#include "n8_memory_map.h"
#include "m6502.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <climits>
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
// ============================================================

void storage_decode(uint64_t& pins, uint8_t reg) {
    if (reg >= N8_DISK_REG_COUNT) {
        // Phantom registers
        if (pins & M6502_RW) M6502_SET_DATA(pins, 0x00);
        return;
    }

    if (pins & M6502_RW) {
        // ---- READ ----
        switch (reg) {
            case N8_DISK_CHAN: {
                uint8_t result = current_channel & N8_DISK_CHAN_MASK;
                if (current_channel < STORAGE_MAX_CHANNELS && channels[current_channel].active) {
                    result |= N8_DISK_CHAN_ACTIVE;
                }
                M6502_SET_DATA(pins, result);
                break;
            }
            case N8_DISK_DATA: {
                if (current_channel == N8_DISK_CONTROL_CHAN) {
                    if (cmd_error_flag) {
                        M6502_SET_DATA(pins, cmd_error_code);
                    } else if (cmd_result_valid) {
                        M6502_SET_DATA(pins, cmd_result);
                        cmd_result_valid = false;
                    } else {
                        M6502_SET_DATA(pins, 0x00);
                    }
                } else if (current_channel < STORAGE_MAX_CHANNELS) {
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
                        if (ch.buf_count == 0 && !ch.eof) {
                            storage_refill_buffer(current_channel);
                        }
                        if (ch.buf_count > 0) {
                            uint8_t byte = ch.read_buf[ch.buf_pos++];
                            ch.buf_count--;
                            M6502_SET_DATA(pins, byte);
                            // Eager refill
                            if (ch.buf_count == 0 && !ch.eof) {
                                storage_refill_buffer(current_channel);
                            }
                        } else {
                            M6502_SET_DATA(pins, 0x00);
                        }
                    }
                } else {
                    M6502_SET_DATA(pins, 0x00);
                }
                break;
            }
            case N8_DISK_ERROR: {
                if (current_channel == N8_DISK_CONTROL_CHAN) {
                    uint8_t err = ctrl_io_error;
                    if (cmd_error_flag) err = N8_DISK_ERR_CMD;
                    M6502_SET_DATA(pins, err);
                } else if (current_channel < STORAGE_MAX_CHANNELS) {
                    M6502_SET_DATA(pins, channels[current_channel].io_error);
                } else {
                    M6502_SET_DATA(pins, 0x00);
                }
                break;
            }
            case N8_DISK_CTRL: {
                M6502_SET_DATA(pins, 0x00);  // write-only
                break;
            }
            case N8_DISK_STATUS: {
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
                    if (ch.eof && ch.buf_count == 0 && ch.type == CHAN_RESPONSE) {
                        storage_close_channel(current_channel);
                    }
                }
                break;
            }
        }
    } else {
        // ---- WRITE ----
        uint8_t val = M6502_GET_DATA(pins);
        switch (reg) {
            case N8_DISK_CHAN: {
                uint8_t id = val & N8_DISK_CHAN_MASK;
                current_channel = id;
                if (id == N8_DISK_CONTROL_CHAN) {
                    ctrl_io_error = 0;
                    cmd_error_flag = false;
                    cmd_error_code = 0;
                } else if (id < STORAGE_MAX_CHANNELS) {
                    channels[id].io_error = 0;
                }
                break;
            }
            case N8_DISK_DATA: {
                if (current_channel == N8_DISK_CONTROL_CHAN) {
                    parser_feed_byte(val);
                } else if (current_channel < STORAGE_MAX_CHANNELS) {
                    channel_t& ch = channels[current_channel];
                    if (!ch.active) {
                        ch.io_error = N8_DISK_IOE_NOT_OPEN;
                    } else if (ch.mode == MODE_READ) {
                        ch.io_error = N8_DISK_IOE_PERMISSION;
                    }
                    // Phase 2: write/append mode handling
                }
                break;
            }
            case N8_DISK_ERROR: {
                // Read-only, ignore writes
                break;
            }
            case N8_DISK_CTRL: {
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
                        if (current_channel < STORAGE_MAX_CHANNELS)
                            channels[current_channel].io_error = N8_DISK_IOE_NOT_OPEN;
                    } else {
                        storage_close_channel(current_channel);
                    }
                }
                break;
            }
            case N8_DISK_STATUS: {
                // Read-only, ignore writes
                break;
            }
        }
    }
}

// ============================================================
// Command Parser
// ============================================================

static void parser_check_opcode() {
    if (cmd_len == 3 &&
        cmd_buf[0] == 'S' && cmd_buf[1] == 'K' && cmd_buf[2] == ',') {
        cmd_in_payload = true;
        cmd_expected_payload = 3;
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

    parser_check_opcode();
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

    // Reject any path component that is ".."
    const char* p = guest_path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
            return false;
        }
        // Skip to next component
        while (*p && *p != '/') p++;
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

static void storage_refill_buffer(int chan_id) {
    channel_t& ch = channels[chan_id];
    if (ch.eof) return;

    if (ch.type == CHAN_FILE) {
        size_t n = fread(ch.read_buf, 1, STORAGE_READ_BUF_SIZE, ch.fp);
        ch.buf_pos = 0;
        ch.buf_count = (int)n;
        if ((int)n < STORAGE_READ_BUF_SIZE)
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
    channel_t& ch = channels[ch_id];
    ch.type = CHAN_RESPONSE;
    ch.mode = MODE_READ;
    ch.active = true;
    ch.response_data = (uint8_t*)malloc(len);
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

    channel_t& ch = channels[ch_id];
    ch.type = CHAN_RESPONSE;
    ch.mode = MODE_READ;
    ch.active = true;
    ch.response_len = (int)response.size();
    ch.response_data = (uint8_t*)malloc(ch.response_len);
    memcpy(ch.response_data, response.data(), ch.response_len);
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

    // Phase 1: read only
    if (mode_char != 'R') {
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

    FILE* fp = fopen(resolved, "rb");
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
    ch.mode = MODE_READ;
    ch.active = true;
    ch.fp = fp;
    strncpy(ch.host_path, resolved, STORAGE_PATH_MAX - 1);
    ch.host_path[STORAGE_PATH_MAX - 1] = '\0';

    storage_refill_buffer(ch_id);

    cmd_result = (uint8_t)ch_id;
    cmd_result_valid = true;
}

static void cmd_close(const char* args) {
    // "CL,<chan_id>" — chan_id is single hex digit 0-E
    if (args[0] != ',') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
        return;
    }

    char hex = args[1];
    int id = -1;
    if (hex >= '0' && hex <= '9') id = hex - '0';
    else if (hex >= 'A' && hex <= 'E') id = hex - 'A' + 10;
    else if (hex >= 'a' && hex <= 'e') id = hex - 'a' + 10;

    if (id < 0 || id >= STORAGE_MAX_CHANNELS) {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_ARG;
        return;
    }

    // Check for trailing garbage
    if (args[2] != '\0') {
        cmd_error_flag = true;
        cmd_error_code = N8_DISK_CE_BAD_SYNTAX;
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
