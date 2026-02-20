// GDB Remote Serial Protocol stub for N8machine
// Zero N8machine includes (D35) — all emulator access through callbacks.

#include "gdb_stub.h"

#include <cstdio>
#include <cstring>
#include <string>

// ---- State ----

static const gdb_stub_callbacks_t* cb = nullptr;
static gdb_stub_config_t config __attribute__((unused));
static bool connected = false;
static bool halted = true;
static bool noack = false;
static int  last_stop_signal = 5; // SIGTRAP
static bool interrupt_flag = false;
static std::string last_response;

// ---- Framing state machine ----

enum frame_state_t {
    FRAME_IDLE,
    FRAME_PACKET_DATA,
    FRAME_CHECKSUM_1,
    FRAME_CHECKSUM_2
};

static frame_state_t frame_state = FRAME_IDLE;
static std::string packet_buf;
static uint8_t recv_checksum;
static uint8_t computed_checksum;

// ---- Hex utilities ----

static int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char to_hex_lo(uint8_t v) {
    const char* hex = "0123456789abcdef";
    return hex[v & 0x0F];
}

static char to_hex_hi(uint8_t v) {
    const char* hex = "0123456789abcdef";
    return hex[(v >> 4) & 0x0F];
}

static std::string to_hex_byte(uint8_t v) {
    std::string s;
    s += to_hex_hi(v);
    s += to_hex_lo(v);
    return s;
}

static std::string to_hex_le16(uint16_t v) {
    std::string s;
    s += to_hex_byte(v & 0xFF);         // low byte first
    s += to_hex_byte((v >> 8) & 0xFF);  // high byte
    return s;
}

// Parse hex string, return -1 on non-hex chars, -2 on overflow > max_val
static int64_t parse_hex(const char* str, size_t len, uint32_t max_val) {
    if (len == 0) return -1;
    if (len > 8) return -2;  // max 32-bit value = 8 hex nibbles
    uint64_t val = 0;
    for (size_t i = 0; i < len; i++) {
        int h = hex_char_val(str[i]);
        if (h < 0) return -1;
        val = (val << 4) | h;
        if (val > 0xFFFFFFFF) return -2; // way too big
    }
    if (val > max_val) return -2;
    return (int64_t)val;
}

static std::string hex_encode(const char* str, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; i++) {
        out += to_hex_byte((uint8_t)str[i]);
    }
    return out;
}

static std::string hex_encode(const std::string& s) {
    return hex_encode(s.data(), s.size());
}

static std::string hex_decode(const char* hex, size_t len) {
    std::string out;
    for (size_t i = 0; i + 1 < len; i += 2) {
        int hi = hex_char_val(hex[i]);
        int lo = hex_char_val(hex[i + 1]);
        if (hi < 0 || lo < 0) return "";
        out += (char)((hi << 4) | lo);
    }
    return out;
}

// ---- Embedded XML blobs ----

static const char target_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
    "<target version=\"1.0\">\n"
    "  <feature name=\"org.n8machine.cpu\">\n"
    "    <reg name=\"a\"     bitsize=\"8\"  type=\"uint8\"    regnum=\"0\"/>\n"
    "    <reg name=\"x\"     bitsize=\"8\"  type=\"uint8\"    regnum=\"1\"/>\n"
    "    <reg name=\"y\"     bitsize=\"8\"  type=\"uint8\"    regnum=\"2\"/>\n"
    "    <reg name=\"sp\"    bitsize=\"8\"  type=\"uint8\"    regnum=\"3\"/>\n"
    "    <reg name=\"pc\"    bitsize=\"16\" type=\"code_ptr\"  regnum=\"4\"/>\n"
    "    <reg name=\"flags\" bitsize=\"8\"  type=\"uint8\"    regnum=\"5\"/>\n"
    "  </feature>\n"
    "</target>\n";

static const char memory_map_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE memory-map SYSTEM \"gdb-memory-map.dtd\">\n"
    "<memory-map>\n"
    "  <memory type=\"ram\"  start=\"0x0000\" length=\"0xC000\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC000\" length=\"0x0100\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC100\" length=\"0x0010\"/>\n"
    "  <memory type=\"ram\"  start=\"0xC110\" length=\"0x0EF0\"/>\n"
    "  <memory type=\"rom\"  start=\"0xD000\" length=\"0x3000\"/>\n"
    "</memory-map>\n";

// ---- Response formatting ----

static std::string format_response(const std::string& payload) {
    uint8_t cksum = 0;
    for (size_t i = 0; i < payload.size(); i++) {
        cksum += (uint8_t)payload[i];
    }
    std::string resp = "$";
    resp += payload;
    resp += '#';
    resp += to_hex_byte(cksum);
    return resp;
}

// Used by Phase 2 TCP layer
static void send_response(const std::string& payload) __attribute__((unused));
static void send_response(const std::string& payload) {
    last_response = format_response(payload);
}

// ---- qXfer chunked read helper ----

static std::string handle_qxfer_read(const char* blob, size_t blob_len,
                                      const char* params) {
    // params is "offset,length" in hex
    const char* comma = strchr(params, ',');
    if (!comma) return "E03";

    int64_t offset = parse_hex(params, comma - params, 0xFFFFFFFF);
    int64_t length = parse_hex(comma + 1, strlen(comma + 1), 0xFFFFFFFF);

    if (offset < 0 || length < 0) return "E03";

    size_t off = (size_t)offset;
    size_t len = (size_t)length;

    if (off >= blob_len) {
        return "l";  // nothing left
    }

    size_t remaining = blob_len - off;
    std::string prefix;
    if (len >= remaining) {
        len = remaining;
        prefix = "l";  // last chunk
    } else {
        prefix = "m";  // more data
    }

    return prefix + std::string(blob + off, len);
}

// ---- Command handlers ----

static std::string handle_question() {
    return "T" + to_hex_byte((uint8_t)last_stop_signal) + "thread:01;";
}

static std::string handle_g() {
    if (!cb) return "E01";
    std::string resp;
    resp += to_hex_byte(cb->read_reg8(0)); // A
    resp += to_hex_byte(cb->read_reg8(1)); // X
    resp += to_hex_byte(cb->read_reg8(2)); // Y
    resp += to_hex_byte(cb->read_reg8(3)); // SP
    resp += to_hex_le16(cb->read_reg16(5)); // PC (little-endian)
    resp += to_hex_byte(cb->read_reg8(4)); // P (flags)
    return resp;
}

static std::string handle_G(const char* data) {
    if (!cb) return "E01";
    size_t len = strlen(data);
    if (len != 14) return "E03";

    // Validate all hex first
    for (size_t i = 0; i < 14; i++) {
        if (hex_char_val(data[i]) < 0) return "E03";
    }

    int64_t a  = parse_hex(data + 0,  2, 0xFF);
    int64_t x  = parse_hex(data + 2,  2, 0xFF);
    int64_t y  = parse_hex(data + 4,  2, 0xFF);
    int64_t sp = parse_hex(data + 6,  2, 0xFF);
    // PC is little-endian: lo byte first
    int64_t pc_lo = parse_hex(data + 8,  2, 0xFF);
    int64_t pc_hi = parse_hex(data + 10, 2, 0xFF);
    int64_t p  = parse_hex(data + 12, 2, 0xFF);

    cb->write_reg8(0, (uint8_t)a);
    cb->write_reg8(1, (uint8_t)x);
    cb->write_reg8(2, (uint8_t)y);
    cb->write_reg8(3, (uint8_t)sp);
    cb->write_reg16(5, (uint16_t)((pc_hi << 8) | pc_lo));
    cb->write_reg8(4, (uint8_t)p);
    return "OK";
}

static std::string handle_p(const char* data) {
    if (!cb) return "E01";
    size_t len = strlen(data);
    if (len == 0) return "E03";

    int64_t reg = parse_hex(data, len, 0xFF);
    if (reg < 0) return "E03";

    switch ((int)reg) {
        case 0: return to_hex_byte(cb->read_reg8(0));  // A
        case 1: return to_hex_byte(cb->read_reg8(1));  // X
        case 2: return to_hex_byte(cb->read_reg8(2));  // Y
        case 3: return to_hex_byte(cb->read_reg8(3));  // SP
        case 4: return to_hex_le16(cb->read_reg16(5)); // PC
        case 5: return to_hex_byte(cb->read_reg8(4));  // P (flags)
        default: return "E02";
    }
}

static std::string handle_P(const char* data) {
    if (!cb) return "E01";
    const char* eq = strchr(data, '=');
    if (!eq) return "E03";

    size_t reg_len = eq - data;
    int64_t reg = parse_hex(data, reg_len, 0xFF);
    if (reg < 0) return "E03";
    if (reg > 5) return "E02";

    const char* val_str = eq + 1;
    size_t val_len = strlen(val_str);

    if ((int)reg == 4) {
        // PC — 4 hex chars, little-endian
        if (val_len != 4) return "E03";
        int64_t lo = parse_hex(val_str, 2, 0xFF);
        int64_t hi = parse_hex(val_str + 2, 2, 0xFF);
        if (lo < 0 || hi < 0) return "E03";
        cb->write_reg16(5, (uint16_t)((hi << 8) | lo));
    } else {
        // 8-bit register — 2 hex chars
        if (val_len != 2) return "E03";
        int64_t val = parse_hex(val_str, 2, 0xFF);
        if (val < 0) return "E03";
        int cb_reg;
        switch ((int)reg) {
            case 0: cb_reg = 0; break; // A
            case 1: cb_reg = 1; break; // X
            case 2: cb_reg = 2; break; // Y
            case 3: cb_reg = 3; break; // SP
            case 5: cb_reg = 4; break; // P
            default: return "E02";
        }
        cb->write_reg8(cb_reg, (uint8_t)val);
    }
    return "OK";
}

static std::string handle_m(const char* data) {
    if (!cb) return "E01";
    const char* comma = strchr(data, ',');
    if (!comma) return "E03";

    int64_t addr = parse_hex(data, comma - data, 0xFFFF);
    int64_t len  = parse_hex(comma + 1, strlen(comma + 1), 0xFFFF);

    if (addr == -1 || len == -1) return "E03";
    if (addr == -2) return "E01";
    if (len == -2) return "E01";
    if ((uint32_t)addr + (uint32_t)len > 0x10000) return "E01";

    if (len == 0) return "";

    std::string resp;
    for (int64_t i = 0; i < len; i++) {
        resp += to_hex_byte(cb->read_mem((uint16_t)(addr + i)));
    }
    return resp;
}

static std::string handle_M(const char* data) {
    if (!cb) return "E01";
    const char* comma = strchr(data, ',');
    if (!comma) return "E03";
    const char* colon = strchr(comma, ':');
    if (!colon) return "E03";

    int64_t addr = parse_hex(data, comma - data, 0xFFFF);
    int64_t len  = parse_hex(comma + 1, colon - comma - 1, 0xFFFF);

    if (addr == -1 || len == -1) return "E03";
    if (addr == -2) return "E01";
    if (len == -2) return "E01";
    if ((uint32_t)addr + (uint32_t)len > 0x10000) return "E01";

    const char* hex_data = colon + 1;
    size_t hex_len = strlen(hex_data);

    if (hex_len != (size_t)(len * 2)) return "E03";

    // Validate all hex chars first
    for (size_t i = 0; i < hex_len; i++) {
        if (hex_char_val(hex_data[i]) < 0) return "E03";
    }

    for (int64_t i = 0; i < len; i++) {
        int hi = hex_char_val(hex_data[i * 2]);
        int lo = hex_char_val(hex_data[i * 2 + 1]);
        cb->write_mem((uint16_t)(addr + i), (uint8_t)((hi << 4) | lo));
    }
    return "OK";
}

static std::string handle_step(const char* data) {
    if (!cb) return "E01";

    // Optional address parameter
    if (data && *data) {
        size_t len = strlen(data);
        int64_t addr = parse_hex(data, len, 0xFFFF);
        if (addr == -1) return "E03";
        if (addr == -2) return "E01";
        cb->write_reg16(5, (uint16_t)addr);
    }

    int sig = cb->step_instruction();
    last_stop_signal = sig;
    halted = true;
    return "T" + to_hex_byte((uint8_t)sig) + "thread:01;";
}

static std::string handle_continue(const char* data) {
    if (!cb) return "E01";

    // Optional address parameter
    if (data && *data) {
        size_t len = strlen(data);
        int64_t addr = parse_hex(data, len, 0xFFFF);
        if (addr == -1) return "E03";
        if (addr == -2) return "E01";
        cb->write_reg16(5, (uint16_t)addr);
    }

    // Phase 1: just set state to running. Async execution deferred to Phase 2.
    halted = false;
    return "";  // no immediate reply for continue (async)
}

static std::string handle_Z(const char* data) {
    if (!cb) return "E01";
    if (strlen(data) < 3) return "E03";

    char kind = data[0];
    if (kind != '0' && kind != '1') return "";  // unsupported Z type → empty

    if (data[1] != ',') return "E03";
    const char* addr_start = data + 2;
    const char* comma2 = strchr(addr_start, ',');
    if (!comma2) return "E03";

    int64_t addr = parse_hex(addr_start, comma2 - addr_start, 0xFFFF);
    if (addr == -1) return "E03";
    if (addr == -2) return "E01";

    cb->set_breakpoint((uint16_t)addr);
    return "OK";
}

static std::string handle_z(const char* data) {
    if (!cb) return "E01";
    if (strlen(data) < 3) return "E03";

    char kind = data[0];
    if (kind != '0' && kind != '1') return "";  // unsupported z type → empty

    if (data[1] != ',') return "E03";
    const char* addr_start = data + 2;
    const char* comma2 = strchr(addr_start, ',');
    if (!comma2) return "E03";

    int64_t addr = parse_hex(addr_start, comma2 - addr_start, 0xFFFF);
    if (addr == -1) return "E03";
    if (addr == -2) return "E01";

    cb->clear_breakpoint((uint16_t)addr);
    return "OK";
}

static std::string handle_H(const char* /*data*/) {
    return "OK";
}

static std::string handle_D() {
    connected = false;
    halted = false;
    return "OK";
}

static std::string handle_qSupported(const char* /*data*/) {
    return "PacketSize=20000;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+";
}

static std::string handle_query(const char* data) {
    size_t len = strlen(data);

    if (len >= 9 && strncmp(data, "Supported", 9) == 0) {
        return handle_qSupported(data + 9);
    }

    // qXfer:features:read:target.xml:offset,length
    if (strncmp(data, "Xfer:features:read:target.xml:", 30) == 0) {
        return handle_qxfer_read(target_xml, strlen(target_xml), data + 30);
    }

    // qXfer:memory-map:read::offset,length
    if (strncmp(data, "Xfer:memory-map:read::", 22) == 0) {
        return handle_qxfer_read(memory_map_xml, strlen(memory_map_xml), data + 22);
    }

    if (strcmp(data, "fThreadInfo") == 0) return "m01";
    if (strcmp(data, "sThreadInfo") == 0) return "l";
    if (strcmp(data, "C") == 0) return "QC01";
    if (strcmp(data, "Attached") == 0) return "1";

    if (strncmp(data, "Rcmd,", 5) == 0) {
        std::string cmd_hex = data + 5;
        std::string cmd = hex_decode(cmd_hex.c_str(), cmd_hex.size());

        if (cmd == "reset") {
            if (cb && cb->reset) cb->reset();
            return "OK";
        }
        // Unknown monitor command
        std::string err_msg = "Unknown monitor command\n";
        return "O" + hex_encode(err_msg) + "OK";  // This is wrong per RSP — should be separate packets
        // Phase 1 simplification: return error text + OK in one response
    }

    return "";  // unknown query → empty
}

static std::string handle_Q(const char* data) {
    if (strncmp(data, "StartNoAckMode", 14) == 0) {
        noack = true;
        return "OK";
    }
    return "";
}

static std::string handle_v(const char* data) {
    if (strcmp(data, "MustReplyEmpty") == 0) return "";
    if (strncmp(data, "Cont?", 5) == 0) return "";
    return "";
}

// ---- Command dispatcher ----

static std::string dispatch_command(const char* payload) {
    if (!payload || !*payload) return "";

    char cmd = payload[0];
    const char* args = payload + 1;

    switch (cmd) {
        case '?': return handle_question();
        case 'g': return handle_g();
        case 'G': return handle_G(args);
        case 'p': return handle_p(args);
        case 'P': return handle_P(args);
        case 'm': return handle_m(args);
        case 'M': return handle_M(args);
        case 's': return handle_step(args);
        case 'c': return handle_continue(args);
        case 'Z': return handle_Z(args);
        case 'z': return handle_z(args);
        case 'H': return handle_H(args);
        case 'D': return handle_D();
        case 'k': connected = false; return "";
        case 'q': return handle_query(args);
        case 'Q': return handle_Q(args);
        case 'v': return handle_v(args);
        default:  return "";  // unknown command → empty
    }
}

// ---- Framing state machine ----

// GDB RSP escape: '}' followed by (byte XOR 0x20).
// Checksum covers raw bytes (including '}' and escaped byte).
// Packet payload contains the decoded value.
static bool escape_next = false;

static void process_complete_packet() {
    if (recv_checksum != computed_checksum) {
        if (!noack) last_response = "-";
        return;
    }

    std::string result = dispatch_command(packet_buf.c_str());
    std::string resp = format_response(result);

    if (!noack) {
        last_response = "+" + resp;
    } else {
        last_response = resp;
    }
}

static void feed_byte_impl(uint8_t byte) {
    switch (frame_state) {
        case FRAME_IDLE:
            if (byte == '$') {
                frame_state = FRAME_PACKET_DATA;
                packet_buf.clear();
                computed_checksum = 0;
                escape_next = false;
            } else if (byte == 0x03) {
                interrupt_flag = true;
                last_stop_signal = 2;
                halted = true;
            }
            // '+' and '-' ignored
            break;

        case FRAME_PACKET_DATA:
            if (byte == '$' && !escape_next) {
                // Restart: abandon current packet, start new one
                packet_buf.clear();
                computed_checksum = 0;
                escape_next = false;
            } else if (byte == '#' && !escape_next) {
                frame_state = FRAME_CHECKSUM_1;
                recv_checksum = 0;
            } else if (byte == '}' && !escape_next) {
                computed_checksum += byte;
                escape_next = true;
            } else {
                computed_checksum += byte;
                if (escape_next) {
                    packet_buf += (char)(byte ^ 0x20);
                    escape_next = false;
                } else {
                    packet_buf += (char)byte;
                }
            }
            break;

        case FRAME_CHECKSUM_1: {
            int h = hex_char_val((char)byte);
            if (h < 0) {
                frame_state = FRAME_IDLE;
                if (!noack) last_response = "-";
                break;
            }
            recv_checksum = (uint8_t)(h << 4);
            frame_state = FRAME_CHECKSUM_2;
            break;
        }

        case FRAME_CHECKSUM_2: {
            int h = hex_char_val((char)byte);
            if (h < 0) {
                frame_state = FRAME_IDLE;
                if (!noack) last_response = "-";
                break;
            }
            recv_checksum |= (uint8_t)h;
            process_complete_packet();
            frame_state = FRAME_IDLE;
            break;
        }
    }
}

// ---- Public API ----

#if ENABLE_GDB_STUB

void gdb_stub_init(const gdb_stub_callbacks_t* callbacks, const gdb_stub_config_t* cfg) {
    cb = callbacks;
    if (cfg) config = *cfg;
    connected = false;
    halted = true;
    noack = false;
    last_stop_signal = 5;
    interrupt_flag = false;
    frame_state = FRAME_IDLE;
    packet_buf.clear();
    last_response.clear();
    escape_next = false;
}

void gdb_stub_shutdown(void) {
    connected = false;
    cb = nullptr;
}

gdb_poll_result_t gdb_stub_poll(void) {
    // Phase 1: no TCP, just return NONE
    return GDB_POLL_NONE;
}

bool gdb_stub_is_connected(void) { return connected; }
bool gdb_stub_is_halted(void) { return halted; }

void gdb_stub_notify_stop(int signal) {
    last_stop_signal = signal;
    halted = true;
}

#endif // ENABLE_GDB_STUB

// ---- Testing API ----

#ifdef GDB_STUB_TESTING

void gdb_stub_feed_byte(uint8_t byte) {
    feed_byte_impl(byte);
}

std::string gdb_stub_process_packet(const char* payload) {
    return dispatch_command(payload);
}

std::string gdb_stub_get_response(void) {
    return last_response;
}

bool gdb_stub_noack_mode(void) {
    return noack;
}

void gdb_stub_reset_state(void) {
    cb = nullptr;
    connected = false;
    halted = true;
    noack = false;
    last_stop_signal = 5;
    interrupt_flag = false;
    frame_state = FRAME_IDLE;
    packet_buf.clear();
    last_response.clear();
    escape_next = false;
}

int gdb_stub_last_signal(void) {
    return last_stop_signal;
}

bool gdb_stub_interrupt_requested(void) {
    bool was = interrupt_flag;
    interrupt_flag = false;
    return was;
}

// Allow tests to set callbacks
void gdb_stub_set_callbacks(const gdb_stub_callbacks_t* callbacks) {
    cb = callbacks;
}

#endif // GDB_STUB_TESTING
