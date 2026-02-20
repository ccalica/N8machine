#include "doctest.h"
#include "gdb_stub.h"
#include <cstring>
#include <string>

// ---- Mock callbacks ----

static uint8_t mock_regs[5];     // A, X, Y, S, P
static uint16_t mock_pc;
static uint8_t mock_mem[65536];
static bool mock_bp[65536];
static int mock_step_signal;
static int mock_stop_reason;
static bool mock_reset_called;

static uint8_t mock_read_reg8(int reg) {
    if (reg >= 0 && reg < 5) return mock_regs[reg];
    return 0;
}

static uint16_t mock_read_reg16(int reg) {
    if (reg == 5) return mock_pc;
    return 0;
}

static void mock_write_reg8(int reg, uint8_t val) {
    if (reg >= 0 && reg < 5) mock_regs[reg] = val;
}

static void mock_write_reg16(int reg, uint16_t val) {
    if (reg == 5) mock_pc = val;
}

static uint8_t mock_read_mem(uint16_t addr) {
    return mock_mem[addr];
}

static void mock_write_mem(uint16_t addr, uint8_t val) {
    mock_mem[addr] = val;
}

static int mock_step_instruction() {
    return mock_step_signal;
}

static void mock_set_breakpoint(uint16_t addr) {
    mock_bp[addr] = true;
}

static void mock_clear_breakpoint(uint16_t addr) {
    mock_bp[addr] = false;
}

static uint16_t mock_get_pc() {
    return mock_pc;
}

static int mock_get_stop_reason() {
    return mock_stop_reason;
}

static void mock_reset() {
    mock_reset_called = true;
}

static const gdb_stub_callbacks_t mock_cb = {
    mock_read_reg8,
    mock_read_reg16,
    mock_write_reg8,
    mock_write_reg16,
    mock_read_mem,
    mock_write_mem,
    mock_step_instruction,
    mock_set_breakpoint,
    mock_clear_breakpoint,
    mock_get_pc,
    mock_get_stop_reason,
    mock_reset
};

// ---- Fixture ----

struct GdbProtocolFixture {
    GdbProtocolFixture() {
        memset(mock_regs, 0, sizeof(mock_regs));
        mock_pc = 0;
        memset(mock_mem, 0, sizeof(mock_mem));
        memset(mock_bp, 0, sizeof(mock_bp));
        mock_step_signal = 5; // SIGTRAP
        mock_stop_reason = 5;
        mock_reset_called = false;
        gdb_stub_reset_state();
        gdb_stub_set_callbacks(&mock_cb);
    }
};

// ---- Helper: compute GDB checksum ----
static std::string make_packet(const std::string& payload) {
    uint8_t cksum = 0;
    for (size_t i = 0; i < payload.size(); i++) {
        cksum += (uint8_t)payload[i];
    }
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", cksum);
    return "$" + payload + "#" + std::string(hex);
}

// Feed a complete packet string byte-by-byte
static void feed_packet(const std::string& raw) {
    for (size_t i = 0; i < raw.size(); i++) {
        gdb_stub_feed_byte((uint8_t)raw[i]);
    }
}

// ---- Helper: check response payload ----
// Extract payload from response string "$payload#XX" or "+$payload#XX"
static std::string extract_payload(const std::string& resp) {
    size_t dollar = resp.find('$');
    size_t hash = resp.rfind('#');
    if (dollar == std::string::npos || hash == std::string::npos || hash <= dollar) {
        return "";
    }
    return resp.substr(dollar + 1, hash - dollar - 1);
}

// ============================================================
TEST_SUITE("gdb_protocol") {

    // ---- Framing tests ----

    TEST_CASE("T01: Valid packet is parsed and ACKed") {
        GdbProtocolFixture f;
        feed_packet(make_packet("?"));
        std::string resp = gdb_stub_get_response();
        CHECK(resp[0] == '+');  // ACK
        CHECK(resp.find('$') != std::string::npos);
    }

    TEST_CASE("T02: Bad checksum produces NACK") {
        GdbProtocolFixture f;
        feed_packet("$?#00");  // wrong checksum for "?"
        std::string resp = gdb_stub_get_response();
        CHECK(resp == "-");
    }

    TEST_CASE("T03: Escape sequence decodes correctly via framing") {
        GdbProtocolFixture f;
        // Test escape decoding through the actual framing state machine.
        // '}' (0x7d) followed by 0x03 should decode to 0x03 ^ 0x20 = 0x23 = '#'
        // We'll construct a memory read "m0,1" but with the '0' after 'm'
        // escaped: '0' = 0x30, escaped = '}' (0x7d) + 0x10 (0x30 ^ 0x20)
        //
        // Payload bytes on wire: m } 0x10 , 1
        // Checksum covers raw bytes: 'm' + '}' + 0x10 + ',' + '1'
        uint8_t raw[] = { 'm', '}', 0x10, ',', '1' };
        uint8_t cksum = 0;
        for (size_t i = 0; i < sizeof(raw); i++) cksum += raw[i];

        // Feed: $ <raw bytes> # <checksum hex>
        gdb_stub_feed_byte('$');
        for (size_t i = 0; i < sizeof(raw); i++) {
            gdb_stub_feed_byte(raw[i]);
        }
        gdb_stub_feed_byte('#');
        char hex_hi[] = "0123456789abcdef";
        gdb_stub_feed_byte((uint8_t)hex_hi[(cksum >> 4) & 0x0F]);
        gdb_stub_feed_byte((uint8_t)hex_hi[cksum & 0x0F]);

        std::string resp = gdb_stub_get_response();
        // Should ACK and return a valid memory read response (2 hex chars)
        CHECK(resp[0] == '+');
        std::string payload = extract_payload(resp);
        CHECK(payload.size() == 2);
    }

    TEST_CASE("T03a: Invalid hex in checksum produces NACK") {
        GdbProtocolFixture f;
        // Feed a packet with non-hex checksum bytes "?#ZZ"
        feed_packet("$?#ZZ");
        std::string resp = gdb_stub_get_response();
        CHECK(resp == "-");
    }

    TEST_CASE("T04: Ctrl-C (0x03) in IDLE sets interrupt flag") {
        GdbProtocolFixture f;
        gdb_stub_feed_byte(0x03);
        CHECK(gdb_stub_interrupt_requested() == true);
    }

    TEST_CASE("T05: Partial packet followed by new $ restarts framing") {
        GdbProtocolFixture f;
        // Feed partial packet, then start a new one
        gdb_stub_feed_byte('$');
        gdb_stub_feed_byte('g');
        // Now start new packet without completing first
        feed_packet(make_packet("?"));
        std::string resp = gdb_stub_get_response();
        std::string payload = extract_payload(resp);
        CHECK(payload.substr(0, 1) == "T");
    }

    TEST_CASE("T07: ACK/NACK bytes in IDLE are ignored") {
        GdbProtocolFixture f;
        gdb_stub_feed_byte('+');
        gdb_stub_feed_byte('-');
        // Should still be in idle, able to process next packet
        feed_packet(make_packet("?"));
        std::string resp = gdb_stub_get_response();
        CHECK(resp.find('$') != std::string::npos);
    }

    TEST_CASE("T10: Multiple packets in sequence") {
        GdbProtocolFixture f;
        feed_packet(make_packet("?"));
        std::string resp1 = gdb_stub_get_response();
        CHECK(resp1[0] == '+');

        feed_packet(make_packet("?"));
        std::string resp2 = gdb_stub_get_response();
        CHECK(resp2[0] == '+');
    }

    // ---- NoAck mode ----

    TEST_CASE("T06: QStartNoAckMode disables ACK prefix") {
        GdbProtocolFixture f;
        // Enable NoAck
        std::string result = gdb_stub_process_packet("QStartNoAckMode");
        CHECK(result == "OK");
        CHECK(gdb_stub_noack_mode() == true);

        // Now feed a packet via framing — no '+' prefix expected
        feed_packet(make_packet("?"));
        std::string resp = gdb_stub_get_response();
        CHECK(resp[0] == '$');  // No '+' prefix
    }

    // ---- Register tests ----

    TEST_CASE("T11: g reads all registers") {
        GdbProtocolFixture f;
        mock_regs[0] = 0x42; // A
        mock_regs[1] = 0x10; // X
        mock_regs[2] = 0xFF; // Y
        mock_regs[3] = 0xFD; // SP
        mock_pc = 0xD000;
        mock_regs[4] = 0x24; // P
        std::string result = gdb_stub_process_packet("g");
        CHECK(result == "4210fffd00d024");
    }

    TEST_CASE("T12: G writes all registers") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("G4210fffd00d024");
        CHECK(result == "OK");
        CHECK(mock_regs[0] == 0x42); // A
        CHECK(mock_regs[1] == 0x10); // X
        CHECK(mock_regs[2] == 0xFF); // Y
        CHECK(mock_regs[3] == 0xFD); // SP
        CHECK(mock_pc == 0xD000);
        CHECK(mock_regs[4] == 0x24); // P
    }

    TEST_CASE("T13: p reads single register (A)") {
        GdbProtocolFixture f;
        mock_regs[0] = 0xAB;
        std::string result = gdb_stub_process_packet("p0");
        CHECK(result == "ab");
    }

    TEST_CASE("T14: p reads PC register (little-endian)") {
        GdbProtocolFixture f;
        mock_pc = 0xD000;
        std::string result = gdb_stub_process_packet("p4");
        CHECK(result == "00d0");
    }

    TEST_CASE("T15: P writes single register") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("P0=ab");
        CHECK(result == "OK");
        CHECK(mock_regs[0] == 0xAB);
    }

    TEST_CASE("T16: P writes PC (little-endian)") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("P4=00d0");
        CHECK(result == "OK");
        CHECK(mock_pc == 0xD000);
    }

    TEST_CASE("T17: p invalid register returns E02") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("p6");
        CHECK(result == "E02");
    }

    TEST_CASE("T18: G wrong length returns E03") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("G42");
        CHECK(result == "E03");
    }

    TEST_CASE("T18a: p reads flags register") {
        GdbProtocolFixture f;
        mock_regs[4] = 0x30; // P (flags)
        std::string result = gdb_stub_process_packet("p5");
        CHECK(result == "30");
    }

    TEST_CASE("T18b: P writes flags register") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("P5=30");
        CHECK(result == "OK");
        CHECK(mock_regs[4] == 0x30);
    }

    // ---- Memory tests ----

    TEST_CASE("T19: m reads single byte") {
        GdbProtocolFixture f;
        mock_mem[0x0200] = 0xAB;
        std::string result = gdb_stub_process_packet("m200,1");
        CHECK(result == "ab");
    }

    TEST_CASE("T20: m reads range") {
        GdbProtocolFixture f;
        mock_mem[0x0100] = 0x01;
        mock_mem[0x0101] = 0x02;
        mock_mem[0x0102] = 0x03;
        std::string result = gdb_stub_process_packet("m100,3");
        CHECK(result == "010203");
    }

    TEST_CASE("T21: M writes single byte") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("M200,1:ab");
        CHECK(result == "OK");
        CHECK(mock_mem[0x0200] == 0xAB);
    }

    TEST_CASE("T22: M writes range") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("M100,3:010203");
        CHECK(result == "OK");
        CHECK(mock_mem[0x0100] == 0x01);
        CHECK(mock_mem[0x0101] == 0x02);
        CHECK(mock_mem[0x0102] == 0x03);
    }

    TEST_CASE("T23: m at boundary 0xFFFF reads one byte") {
        GdbProtocolFixture f;
        mock_mem[0xFFFF] = 0x42;
        std::string result = gdb_stub_process_packet("mffff,1");
        CHECK(result == "42");
    }

    TEST_CASE("T24: m overflow returns E01") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("mffff,2");
        CHECK(result == "E01");
    }

    TEST_CASE("T25: m reads ROM area") {
        GdbProtocolFixture f;
        mock_mem[0xD000] = 0xEA;
        std::string result = gdb_stub_process_packet("md000,1");
        CHECK(result == "ea");
    }

    TEST_CASE("T26: M writes to ROM area (via callback)") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("Md000,1:ea");
        CHECK(result == "OK");
        CHECK(mock_mem[0xD000] == 0xEA);
    }

    TEST_CASE("T27: m reads device region (via callback)") {
        GdbProtocolFixture f;
        mock_mem[0xC100] = 0x55;
        std::string result = gdb_stub_process_packet("mc100,1");
        CHECK(result == "55");
    }

    TEST_CASE("T28: m non-hex address returns E03") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("mXYZZ,1");
        CHECK(result == "E03");
    }

    TEST_CASE("T29: m address > 0xFFFF returns E01") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("m10000,1");
        CHECK(result == "E01");
    }

    TEST_CASE("T74: m zero length returns empty") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("m100,0");
        CHECK(result == "");
    }

    // ---- Breakpoint tests ----

    TEST_CASE("T30: Z0 sets software breakpoint") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("Z0,d000,1");
        CHECK(result == "OK");
        CHECK(mock_bp[0xD000] == true);
    }

    TEST_CASE("T31: z0 clears software breakpoint") {
        GdbProtocolFixture f;
        mock_bp[0xD000] = true;
        std::string result = gdb_stub_process_packet("z0,d000,1");
        CHECK(result == "OK");
        CHECK(mock_bp[0xD000] == false);
    }

    TEST_CASE("T32: Z1 maps to same mechanism as Z0") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("Z1,d010,1");
        CHECK(result == "OK");
        CHECK(mock_bp[0xD010] == true);
    }

    TEST_CASE("T33: Z2-Z4 return empty (unsupported)") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("Z2,d000,1") == "");
        CHECK(gdb_stub_process_packet("Z3,d000,1") == "");
        CHECK(gdb_stub_process_packet("Z4,d000,1") == "");
    }

    TEST_CASE("T76: Z0 at boundary addresses") {
        GdbProtocolFixture f;
        std::string r1 = gdb_stub_process_packet("Z0,0,1");
        CHECK(r1 == "OK");
        CHECK(mock_bp[0x0000] == true);

        std::string r2 = gdb_stub_process_packet("Z0,ffff,1");
        CHECK(r2 == "OK");
        CHECK(mock_bp[0xFFFF] == true);
    }

    TEST_CASE("T77: Multiple breakpoints") {
        GdbProtocolFixture f;
        gdb_stub_process_packet("Z0,d000,1");
        gdb_stub_process_packet("Z0,d010,1");
        gdb_stub_process_packet("Z0,d020,1");
        CHECK(mock_bp[0xD000] == true);
        CHECK(mock_bp[0xD010] == true);
        CHECK(mock_bp[0xD020] == true);
    }

    TEST_CASE("T78: z1 clears same as z0") {
        GdbProtocolFixture f;
        mock_bp[0xD000] = true;
        std::string result = gdb_stub_process_packet("z1,d000,1");
        CHECK(result == "OK");
        CHECK(mock_bp[0xD000] == false);
    }

    TEST_CASE("T79: Idempotent breakpoint set") {
        GdbProtocolFixture f;
        gdb_stub_process_packet("Z0,d000,1");
        gdb_stub_process_packet("Z0,d000,1");
        CHECK(mock_bp[0xD000] == true);
        // Clear once should be enough
        gdb_stub_process_packet("z0,d000,1");
        CHECK(mock_bp[0xD000] == false);
    }

    // ---- Step tests ----

    TEST_CASE("T37: Step returns SIGTRAP stop reply") {
        GdbProtocolFixture f;
        mock_step_signal = 5;
        std::string result = gdb_stub_process_packet("s");
        CHECK(result == "T05thread:01;");
    }

    TEST_CASE("T40: Step with JAM returns SIGILL") {
        GdbProtocolFixture f;
        mock_step_signal = 4; // SIGILL
        std::string result = gdb_stub_process_packet("s");
        CHECK(result == "T04thread:01;");
    }

    TEST_CASE("T41: Step with address parameter") {
        GdbProtocolFixture f;
        mock_step_signal = 5;
        std::string result = gdb_stub_process_packet("sd000");
        CHECK(result == "T05thread:01;");
        CHECK(mock_pc == 0xD000);
    }

    // ---- Query tests ----

    TEST_CASE("T55: qSupported returns capabilities") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("qSupported");
        CHECK(result.find("PacketSize=20000") != std::string::npos);
        CHECK(result.find("QStartNoAckMode+") != std::string::npos);
        CHECK(result.find("qXfer:features:read+") != std::string::npos);
        CHECK(result.find("qXfer:memory-map:read+") != std::string::npos);
    }

    TEST_CASE("T56: target XML contains org.n8machine.cpu") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("qXfer:features:read:target.xml:0,fff");
        CHECK(result[0] == 'l'); // last chunk (fits in one)
        CHECK(result.find("org.n8machine.cpu") != std::string::npos);
    }

    TEST_CASE("T57: target XML chunked read") {
        GdbProtocolFixture f;
        // Read only 16 bytes
        std::string r1 = gdb_stub_process_packet("qXfer:features:read:target.xml:0,10");
        CHECK(r1[0] == 'm'); // more data
        CHECK(r1.size() == 17); // 'm' + 16 bytes

        // Read rest
        std::string r2 = gdb_stub_process_packet("qXfer:features:read:target.xml:10,fff");
        CHECK(r2[0] == 'l'); // last chunk
    }

    TEST_CASE("T58: memory map XML") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("qXfer:memory-map:read::0,fff");
        CHECK(result[0] == 'l');
        CHECK(result.find("memory-map") != std::string::npos);
        CHECK(result.find("0xD000") != std::string::npos);
        CHECK(result.find("rom") != std::string::npos);
    }

    TEST_CASE("T59: qfThreadInfo returns m01") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("qfThreadInfo") == "m01");
    }

    TEST_CASE("T60: qsThreadInfo returns l") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("qsThreadInfo") == "l");
    }

    TEST_CASE("T61: qC returns QC01") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("qC") == "QC01");
    }

    TEST_CASE("T62: qAttached returns 1") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("qAttached") == "1");
    }

    TEST_CASE("T63: qRcmd reset calls callback") {
        GdbProtocolFixture f;
        // "reset" hex-encoded = "7265736574"
        std::string result = gdb_stub_process_packet("qRcmd,7265736574");
        CHECK(result == "OK");
        CHECK(mock_reset_called == true);
    }

    TEST_CASE("T63a: qRcmd unknown command returns error") {
        GdbProtocolFixture f;
        // "foo" hex = "666f6f"
        std::string result = gdb_stub_process_packet("qRcmd,666f6f");
        CHECK(result.find("O") == 0); // starts with output packet
    }

    // ---- Stop reason tests ----

    TEST_CASE("T64: ? returns SIGTRAP by default") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("?");
        CHECK(result == "T05thread:01;");
    }

    TEST_CASE("T65: SIGILL persists in stop reason") {
        GdbProtocolFixture f;
        mock_step_signal = 4;
        gdb_stub_process_packet("s"); // step triggers SIGILL
        std::string result = gdb_stub_process_packet("?");
        CHECK(result == "T04thread:01;");
    }

    // ---- Edge cases ----

    TEST_CASE("T09: Unknown command returns empty") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("!");
        CHECK(result == "");
    }

    TEST_CASE("T72: M with non-hex data returns E03") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("M200,1:XY");
        CHECK(result == "E03");
    }

    TEST_CASE("T73: M with wrong data length returns E03") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("M200,2:ab");
        CHECK(result == "E03");
    }

    TEST_CASE("H command returns OK") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("Hg0") == "OK");
        CHECK(gdb_stub_process_packet("Hc0") == "OK");
    }

    TEST_CASE("D detach returns OK") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("D") == "OK");
    }

    TEST_CASE("vMustReplyEmpty returns empty") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("vMustReplyEmpty") == "");
    }

    TEST_CASE("vCont? returns empty (not supported in Phase 1)") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("vCont?") == "");
    }

    TEST_CASE("P with invalid register returns E02") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("Pa=42") == "E02");
    }

    TEST_CASE("P with wrong value length returns E03") {
        GdbProtocolFixture f;
        CHECK(gdb_stub_process_packet("P0=abcd") == "E03");
    }

    TEST_CASE("c (continue) sets non-halted state") {
        GdbProtocolFixture f;
        std::string result = gdb_stub_process_packet("c");
        // Phase 1: continue is a no-op, returns empty
        CHECK(result == "");
    }

    TEST_CASE("c with address parameter sets PC") {
        GdbProtocolFixture f;
        gdb_stub_process_packet("cd000");
        CHECK(mock_pc == 0xD000);
    }

    TEST_CASE("Ctrl-C sets SIGINT as last signal") {
        GdbProtocolFixture f;
        gdb_stub_feed_byte(0x03);
        CHECK(gdb_stub_last_signal() == 2); // SIGINT
    }

} // TEST_SUITE("gdb_protocol")
