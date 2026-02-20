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
    if (kind != '0' && kind != '1' && kind != '2' && kind != '3' && kind != '4')
        return "";  // unsupported Z type → empty

    if (data[1] != ',') return "E03";
    const char* addr_start = data + 2;
    const char* comma2 = strchr(addr_start, ',');
    if (!comma2) return "E03";

    int64_t addr = parse_hex(addr_start, comma2 - addr_start, 0xFFFF);
    if (addr == -1) return "E03";
    if (addr == -2) return "E01";

    if (kind == '0' || kind == '1') {
        cb->set_breakpoint((uint16_t)addr);
    } else {
        if (!cb->set_watchpoint) return "";  // no callback = unsupported
        cb->set_watchpoint((uint16_t)addr, kind - '0');
    }
    return "OK";
}

static std::string handle_z(const char* data) {
    if (!cb) return "E01";
    if (strlen(data) < 3) return "E03";

    char kind = data[0];
    if (kind != '0' && kind != '1' && kind != '2' && kind != '3' && kind != '4')
        return "";  // unsupported z type → empty

    if (data[1] != ',') return "E03";
    const char* addr_start = data + 2;
    const char* comma2 = strchr(addr_start, ',');
    if (!comma2) return "E03";

    int64_t addr = parse_hex(addr_start, comma2 - addr_start, 0xFFFF);
    if (addr == -1) return "E03";
    if (addr == -2) return "E01";

    if (kind == '0' || kind == '1') {
        cb->clear_breakpoint((uint16_t)addr);
    } else {
        if (!cb->clear_watchpoint) return "";  // no callback = unsupported
        cb->clear_watchpoint((uint16_t)addr, kind - '0');
    }
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
    if (strcmp(data, "Cont?") == 0) return "vCont;c;s;t";
    if (strncmp(data, "Cont;", 5) == 0) {
        char action = data[5];
        const char* rest = data + 6;
        // Skip optional :thread-id (and trailing ; separator)
        if (*rest == ':') {
            rest++; // skip ':'
            while (*rest && *rest != ';') rest++; // skip thread-id
            if (*rest == ';') rest++; // skip action separator
        }
        if (action == 'c') return handle_continue("");
        if (action == 's') return handle_step("");
        if (action == 't') {
            halted = true;
            last_stop_signal = 2;
            return "T02thread:01;";
        }
        return "";
    }
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

// ---- Public API (Phase 2 — TCP transport) ----

#if ENABLE_GDB_STUB

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>

// ---- TCP transport state ----

static std::thread* tcp_thread_ptr = nullptr;
static std::mutex cmd_mutex;
static std::queue<std::string> cmd_queue;
static std::mutex resp_mutex;
static std::condition_variable resp_cv;
static std::queue<std::string> resp_queue;
static std::atomic<bool> gdb_shutdown{false};
static std::atomic<bool> interrupt_requested_flag{false};
static std::atomic<bool> client_connected_flag{false};
static std::atomic<bool> tcp_noack_mode{false};
static std::atomic<int> server_fd{-1};

// Sentinels (octal \001 prefix distinguishes from GDB packets)
static const std::string SENT_CONNECT    = "\001CONNECT";
static const std::string SENT_DISCONNECT = "\001DISCONNECT";
static const std::string SENT_INTERRUPT  = "\001INTERRUPT";
static const std::string SENT_CONTINUE   = "\001CONTINUE";
static const std::string SENT_NOREPLY    = "\001NOREPLY";

static bool is_sentinel(const std::string& s) {
    return !s.empty() && s[0] == '\001';
}

// ---- TCP thread ----

static void tcp_thread_func(int port) {
    int local_client_fd = -1;
    try {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            fprintf(stderr, "GDB stub: socket() failed: %s\n", strerror(errno));
            return;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)port);

        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            fprintf(stderr, "GDB stub: bind() failed: %s\n", strerror(errno));
            close(server_fd); server_fd = -1;
            return;
        }
        if (listen(server_fd, 1) < 0) {
            fprintf(stderr, "GDB stub: listen() failed: %s\n", strerror(errno));
            close(server_fd); server_fd = -1;
            return;
        }

        printf("GDB stub: listening on port %d\n", port);

        // Accept loop
        while (!gdb_shutdown.load()) {
            struct pollfd pfd;
            pfd.fd = server_fd;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, 200);
            if (ret <= 0) continue;

            local_client_fd = accept(server_fd, nullptr, nullptr);
            if (local_client_fd < 0) continue;

            // Set recv timeout 100ms
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            setsockopt(local_client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            client_connected_flag.store(true);
            tcp_noack_mode.store(false);

            // Enqueue connect sentinel
            {
                std::lock_guard<std::mutex> lk(cmd_mutex);
                cmd_queue.push(SENT_CONNECT);
            }

            printf("GDB stub: client connected\n");

            // Local framing state for this connection
            enum { IDLE, DATA, CKSUM1, CKSUM2 } fstate = IDLE;
            std::string pktbuf;
            uint8_t rcksum = 0, ccksum = 0;
            bool esc = false;
            bool waiting_async = false;

            // Client loop
            while (!gdb_shutdown.load() && client_connected_flag.load()) {
                uint8_t buf[1024];
                ssize_t n = recv(local_client_fd, buf, sizeof(buf), 0);

                if (n == 0) break; // client disconnected

                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // Recv timeout — check for async stop reply
                        if (waiting_async) {
                            std::unique_lock<std::mutex> lk(resp_mutex);
                            if (!resp_queue.empty()) {
                                std::string resp = resp_queue.front();
                                resp_queue.pop();
                                lk.unlock();
                                if (!is_sentinel(resp)) {
                                    std::string framed = format_response(resp);
                                    send(local_client_fd, framed.c_str(), framed.size(), MSG_NOSIGNAL);
                                }
                                waiting_async = false;
                            }
                        }
                        continue;
                    }
                    break; // real error
                }

                // Process received bytes
                for (ssize_t i = 0; i < n; i++) {
                    uint8_t byte = buf[i];

                    switch (fstate) {
                    case IDLE:
                        if (byte == '$') {
                            fstate = DATA;
                            pktbuf.clear();
                            ccksum = 0;
                            esc = false;
                        } else if (byte == 0x03) {
                            interrupt_requested_flag.store(true);
                            {
                                std::lock_guard<std::mutex> lk(cmd_mutex);
                                cmd_queue.push(SENT_INTERRUPT);
                            }
                            // Enter async wait so recv timeout picks up the stop reply
                            waiting_async = true;
                        }
                        break;

                    case DATA:
                        if (byte == '$' && !esc) {
                            pktbuf.clear(); ccksum = 0; esc = false;
                        } else if (byte == '#' && !esc) {
                            fstate = CKSUM1; rcksum = 0;
                        } else if (byte == '}' && !esc) {
                            ccksum += byte; esc = true;
                        } else if (pktbuf.size() >= 20000) {
                            // PacketSize exceeded — discard
                            fstate = IDLE;
                            if (!tcp_noack_mode.load()) {
                                char nak = '-';
                                send(local_client_fd, &nak, 1, MSG_NOSIGNAL);
                            }
                        } else {
                            ccksum += byte;
                            pktbuf += esc ? (char)(byte ^ 0x20) : (char)byte;
                            esc = false;
                        }
                        break;

                    case CKSUM1: {
                        int h = hex_char_val((char)byte);
                        if (h < 0) {
                            fstate = IDLE;
                            if (!tcp_noack_mode.load()) {
                                char nak = '-';
                                send(local_client_fd, &nak, 1, MSG_NOSIGNAL);
                            }
                            break;
                        }
                        rcksum = (uint8_t)(h << 4);
                        fstate = CKSUM2;
                        break;
                    }

                    case CKSUM2: {
                        int h = hex_char_val((char)byte);
                        if (h < 0) {
                            fstate = IDLE;
                            if (!tcp_noack_mode.load()) {
                                char nak = '-';
                                send(local_client_fd, &nak, 1, MSG_NOSIGNAL);
                            }
                            break;
                        }
                        rcksum |= (uint8_t)h;

                        if (rcksum != ccksum) {
                            // Bad checksum — NAK
                            if (!tcp_noack_mode.load()) {
                                char nak = '-';
                                send(local_client_fd, &nak, 1, MSG_NOSIGNAL);
                            }
                        } else {
                            // Good checksum — ACK
                            if (!tcp_noack_mode.load()) {
                                char ack = '+';
                                send(local_client_fd, &ack, 1, MSG_NOSIGNAL);
                            }

                            // Drain pending async stop reply if needed
                            if (waiting_async) {
                                std::unique_lock<std::mutex> lk(resp_mutex);
                                while (!resp_queue.empty()) {
                                    std::string pending = resp_queue.front();
                                    resp_queue.pop();
                                    if (!is_sentinel(pending)) {
                                        lk.unlock();
                                        std::string framed = format_response(pending);
                                        send(local_client_fd, framed.c_str(), framed.size(), MSG_NOSIGNAL);
                                        lk.lock();
                                    }
                                }
                                waiting_async = false;
                            }

                            // Enqueue command for main thread
                            {
                                std::lock_guard<std::mutex> lk(cmd_mutex);
                                cmd_queue.push(pktbuf);
                            }

                            // Wait for response from main thread
                            {
                                std::unique_lock<std::mutex> lk(resp_mutex);
                                bool got = resp_cv.wait_for(lk,
                                    std::chrono::milliseconds(500),
                                    []{ return !resp_queue.empty() || gdb_shutdown.load(); });

                                if (got) {
                                    if (gdb_shutdown.load()) break;
                                    std::string resp = resp_queue.front();
                                    resp_queue.pop();
                                    lk.unlock();

                                    if (resp == SENT_CONTINUE) {
                                        waiting_async = true;
                                    } else if (!is_sentinel(resp)) {
                                        std::string framed = format_response(resp);
                                        send(local_client_fd, framed.c_str(), framed.size(), MSG_NOSIGNAL);
                                    }
                                } else {
                                    lk.unlock();
                                    fprintf(stderr, "[gdb_stub] response timeout, sending empty reply\n");
                                    std::string framed = format_response("");
                                    send(local_client_fd, framed.c_str(), framed.size(), MSG_NOSIGNAL);
                                }
                            }
                        }

                        fstate = IDLE;
                        break;
                    }
                    } // switch
                } // for each byte
            } // client loop

            // Client disconnected
            {
                std::lock_guard<std::mutex> lk(cmd_mutex);
                cmd_queue.push(SENT_DISCONNECT);
            }
            close(local_client_fd);
            local_client_fd = -1;
            client_connected_flag.store(false);
            printf("GDB stub: client disconnected\n");
        } // accept loop

    } catch (...) {
        fprintf(stderr, "GDB stub: TCP thread exception\n");
    }

    // Cleanup
    if (local_client_fd >= 0) close(local_client_fd);
    if (server_fd >= 0) { close(server_fd); server_fd = -1; }
}

// ---- Priority helper ----

static bool higher_poll_priority(gdb_poll_result_t a, gdb_poll_result_t b) {
    // KILL > DETACHED > HALTED > STEPPED > RESUMED > NONE
    static const int prio[] = { 0, 3, 1, 2, 4, 5 };
    return prio[(int)a] > prio[(int)b];
}

// ---- Public API ----

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

    // TCP transport
    gdb_shutdown.store(false);
    interrupt_requested_flag.store(false);
    client_connected_flag.store(false);
    tcp_noack_mode.store(false);
    if (config.enabled) {
        tcp_thread_ptr = new std::thread(tcp_thread_func, config.port);
    }
}

void gdb_stub_shutdown(void) {
    gdb_shutdown.store(true);
    resp_cv.notify_all();
    if (tcp_thread_ptr) {
        if (server_fd >= 0) ::shutdown(server_fd, SHUT_RDWR);
        tcp_thread_ptr->join();
        delete tcp_thread_ptr;
        tcp_thread_ptr = nullptr;
    }
    connected = false;
    cb = nullptr;
}

gdb_poll_result_t gdb_stub_poll(void) {
    gdb_poll_result_t result = GDB_POLL_NONE;

    // Drain command queue (unconditional — SENT_DISCONNECT may arrive after flag clears)
    std::queue<std::string> local_q;
    {
        std::lock_guard<std::mutex> lk(cmd_mutex);
        std::swap(local_q, cmd_queue);
    }

    while (!local_q.empty()) {
        std::string cmd = local_q.front();
        local_q.pop();

        gdb_poll_result_t r = GDB_POLL_NONE;

        if (is_sentinel(cmd)) {
            if (cmd == SENT_CONNECT) {
                connected = true;
                halted = true;
                noack = false;
                tcp_noack_mode.store(false);
                last_stop_signal = 5;
                // D23/D49: align to SYNC boundary
                if (cb && cb->get_pc && cb->write_reg16) {
                    uint16_t pc = cb->get_pc();
                    cb->write_reg16(5, pc);
                }
                r = GDB_POLL_HALTED;
            } else if (cmd == SENT_DISCONNECT) {
                connected = false;
                halted = false;
                r = GDB_POLL_DETACHED;
            } else if (cmd == SENT_INTERRUPT) {
                interrupt_requested_flag.store(false);
                halted = true;
                last_stop_signal = 2;
                // Push stop reply for TCP thread
                {
                    std::lock_guard<std::mutex> lk(resp_mutex);
                    resp_queue.push("T" + to_hex_byte(2) + "thread:01;");
                }
                resp_cv.notify_one();
                r = GDB_POLL_HALTED;
            }
        } else if (!cmd.empty()) {
            char first = cmd[0];

            bool is_continue = (first == 'c') ||
                               (cmd.compare(0, 7, "vCont;c") == 0);
            bool is_step = (first == 's') ||
                           (cmd.compare(0, 7, "vCont;s") == 0);
            bool is_vcont_t = (cmd.compare(0, 7, "vCont;t") == 0);

            if (is_continue) {
                // Continue: dispatch for side effects (optional PC set)
                dispatch_command(cmd.c_str());
                {
                    std::lock_guard<std::mutex> lk(resp_mutex);
                    resp_queue.push(SENT_CONTINUE);
                }
                resp_cv.notify_one();
                r = GDB_POLL_RESUMED;
            } else if (is_vcont_t) {
                // vCont;t — halt (same as interrupt)
                dispatch_command(cmd.c_str());
                {
                    std::lock_guard<std::mutex> lk(resp_mutex);
                    resp_queue.push("T02thread:01;");
                }
                resp_cv.notify_one();
                r = GDB_POLL_HALTED;
            } else if (first == 'D') {
                std::string resp = dispatch_command(cmd.c_str());
                {
                    std::lock_guard<std::mutex> lk(resp_mutex);
                    resp_queue.push(resp);
                }
                resp_cv.notify_one();
                r = GDB_POLL_DETACHED;
            } else if (first == 'k') {
                connected = false;
                {
                    std::lock_guard<std::mutex> lk(resp_mutex);
                    resp_queue.push(SENT_NOREPLY);
                }
                resp_cv.notify_one();
                r = GDB_POLL_KILL;
            } else {
                // All other commands (including 's')
                std::string resp = dispatch_command(cmd.c_str());
                // Propagate noack mode to TCP thread
                if (noack) tcp_noack_mode.store(true);
                {
                    std::lock_guard<std::mutex> lk(resp_mutex);
                    resp_queue.push(resp);
                }
                resp_cv.notify_one();
                if (is_step) r = GDB_POLL_STEPPED;
            }
        }

        if (higher_poll_priority(r, result)) result = r;
    }

    return result;
}

bool gdb_stub_is_connected(void) { return connected; }
bool gdb_stub_is_halted(void) { return halted; }

void gdb_stub_notify_stop(int signal) {
    last_stop_signal = signal;
    halted = true;
    std::string stop_reply = "T" + to_hex_byte((uint8_t)signal) + "thread:01;";
    {
        std::lock_guard<std::mutex> lk(resp_mutex);
        resp_queue.push(stop_reply);
    }
    resp_cv.notify_one();
}

bool gdb_interrupt_requested(void) {
    return interrupt_requested_flag.exchange(false);
}

int gdb_stub_get_step_guard(void) {
    return (config.step_guard > 0) ? config.step_guard : 16;
}

void gdb_stub_notify_watchpoint(uint16_t addr, int type) {
    last_stop_signal = 5;  // SIGTRAP
    halted = true;
    const char* wp_type_str = (type == 2) ? "watch" :
                              (type == 3) ? "rwatch" : "awatch";
    std::string stop_reply = "T05" + std::string(wp_type_str) + ":" +
                             to_hex_le16(addr) + ";thread:01;";
    {
        std::lock_guard<std::mutex> lk(resp_mutex);
        resp_queue.push(stop_reply);
    }
    resp_cv.notify_one();
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
