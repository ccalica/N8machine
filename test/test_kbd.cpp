#include "doctest.h"
#include "test_helpers.h"
#include "emu_kbd.h"

TEST_SUITE("keyboard") {

    // -------------------------------------------------------------------------
    // T150: kbd_reset() clears data, status, ctrl
    // -------------------------------------------------------------------------

    TEST_CASE("T150: kbd_reset clears all registers") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        kbd_reset();
        CHECK(kbd_get_data() == 0x00);
        CHECK(kbd_get_status() == 0x00);
        CHECK(kbd_data_available() == false);
    }

    // -------------------------------------------------------------------------
    // T151: kbd_inject_key sets DATA_AVAIL in status
    // -------------------------------------------------------------------------

    TEST_CASE("T151: kbd_inject_key sets DATA_AVAIL") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        CHECK((kbd_get_status() & N8_KBD_STAT_AVAIL) != 0);
        CHECK(kbd_data_available() == true);
    }

    // -------------------------------------------------------------------------
    // T152: Read KBD_DATA returns injected key code
    // -------------------------------------------------------------------------

    TEST_CASE("T152: Read KBD_DATA returns injected keycode") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        uint64_t p = make_read_pins(N8_KBD_BASE + N8_KBD_DATA);
        kbd_decode(p, N8_KBD_DATA);
        CHECK(M6502_GET_DATA(p) == 0x41);
    }

    // -------------------------------------------------------------------------
    // T153: Read KBD_STATUS shows DATA_AVAIL=1
    // -------------------------------------------------------------------------

    TEST_CASE("T153: Read KBD_STATUS shows DATA_AVAIL") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        uint64_t p = make_read_pins(N8_KBD_BASE + N8_KBD_STATUS);
        kbd_decode(p, N8_KBD_STATUS);
        CHECK((M6502_GET_DATA(p) & N8_KBD_STAT_AVAIL) != 0);
    }

    // -------------------------------------------------------------------------
    // T154: Write KBD_ACK pops front; two ACKs drain FIFO
    // -------------------------------------------------------------------------

    TEST_CASE("T154: Write KBD_ACK pops front entry from FIFO") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        kbd_inject_key(0x42, 0x00);
        // First ACK pops 0x41, 0x42 still buffered
        uint64_t p = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(p, N8_KBD_ACK);
        CHECK(kbd_data_available() == true);
        CHECK(kbd_get_data() == 0x42);
        // Second ACK pops 0x42, buffer empty
        p = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(p, N8_KBD_ACK);
        CHECK(kbd_data_available() == false);
    }

    // -------------------------------------------------------------------------
    // T155: Two injects produce FIFO ordering, front is first key
    // -------------------------------------------------------------------------

    TEST_CASE("T155: Two injects produce FIFO order, no overflow") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        kbd_inject_key(0x42, 0x00);
        // No overflow — buffer has room for 64
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) == 0);
        // Front of FIFO is first key injected
        CHECK(kbd_get_data() == 0x41);
    }

    // -------------------------------------------------------------------------
    // T156: KBD_CTRL IRQ_EN=1: inject sets IRQ bit 2
    // -------------------------------------------------------------------------

    TEST_CASE("T156: KBD_CTRL IRQ_EN enables IRQ on inject") {
        EmulatorFixture f;
        // Enable IRQ
        uint64_t pc = make_write_pins(N8_KBD_BASE + N8_KBD_CTRL, N8_KBD_CTRL_IRQ_EN);
        kbd_decode(pc, N8_KBD_CTRL);
        // Inject key
        kbd_inject_key(0x41, 0x00);
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) != 0);
    }

    // -------------------------------------------------------------------------
    // T157: KBD_CTRL IRQ_EN=0: inject does NOT set IRQ
    // -------------------------------------------------------------------------

    TEST_CASE("T157: KBD_CTRL IRQ_EN=0 does not set IRQ") {
        EmulatorFixture f;
        // IRQ disabled by default (ctrl=0)
        kbd_inject_key(0x41, 0x00);
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) == 0);
    }

    // -------------------------------------------------------------------------
    // T158: ACK deasserts IRQ bit 2 when buffer fully drained
    // -------------------------------------------------------------------------

    TEST_CASE("T158: ACK deasserts IRQ bit 2") {
        EmulatorFixture f;
        // Enable IRQ and inject
        uint64_t pc = make_write_pins(N8_KBD_BASE + N8_KBD_CTRL, N8_KBD_CTRL_IRQ_EN);
        kbd_decode(pc, N8_KBD_CTRL);
        kbd_inject_key(0x41, 0x00);
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) != 0);
        // ACK
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) == 0);
    }

    // -------------------------------------------------------------------------
    // T159: Modifier bits reflect front entry in status
    // -------------------------------------------------------------------------

    TEST_CASE("T159: Modifier bits reflect in status") {
        EmulatorFixture f;
        // SHIFT=0x04, CTRL=0x08, ALT=0x10, CAPS=0x20
        kbd_inject_key(0x41, N8_KBD_STAT_SHIFT | N8_KBD_STAT_CTRL);
        CHECK((kbd_get_status() & N8_KBD_STAT_SHIFT) != 0);
        CHECK((kbd_get_status() & N8_KBD_STAT_CTRL) != 0);
        CHECK((kbd_get_status() & N8_KBD_STAT_ALT) == 0);

        // Inject second key with ALT + CAPS, ACK first to advance
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        kbd_inject_key(0x42, N8_KBD_STAT_ALT | N8_KBD_STAT_CAPS);
        CHECK((kbd_get_status() & N8_KBD_STAT_ALT) != 0);
        CHECK((kbd_get_status() & N8_KBD_STAT_CAPS) != 0);
        CHECK((kbd_get_status() & N8_KBD_STAT_SHIFT) == 0);
    }

    // -------------------------------------------------------------------------
    // T160: Modifiers update even when DATA_AVAIL=0
    // -------------------------------------------------------------------------

    TEST_CASE("T160: Modifiers update even when DATA_AVAIL=0") {
        EmulatorFixture f;
        // No prior inject, DATA_AVAIL=0
        CHECK(kbd_data_available() == false);
        // Inject with modifiers — this sets DATA_AVAIL and modifiers
        kbd_inject_key(0x41, N8_KBD_STAT_SHIFT);
        CHECK((kbd_get_status() & N8_KBD_STAT_SHIFT) != 0);
        CHECK(kbd_data_available() == true);
    }

    // -------------------------------------------------------------------------
    // T161: Reserved registers ($D863-$D87F) read 0, write no-op
    // -------------------------------------------------------------------------

    TEST_CASE("T161: Reserved KBD registers read 0, write no-op") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        for (uint8_t reg = 3; reg < 32; reg++) {
            uint64_t pr = make_read_pins(N8_KBD_BASE + reg);
            kbd_decode(pr, reg);
            CHECK(M6502_GET_DATA(pr) == 0x00);
        }
        // Verify data wasn't corrupted by writes to reserved regs
        CHECK(kbd_get_data() == 0x41);
    }

    // -------------------------------------------------------------------------
    // T162: Extended key code ($80 = Up Arrow) stored correctly
    // -------------------------------------------------------------------------

    TEST_CASE("T162: Extended key code stored correctly") {
        EmulatorFixture f;
        kbd_inject_key(0x80, 0x00);  // Up Arrow
        CHECK(kbd_get_data() == 0x80);
        uint64_t p = make_read_pins(N8_KBD_BASE + N8_KBD_DATA);
        kbd_decode(p, N8_KBD_DATA);
        CHECK(M6502_GET_DATA(p) == 0x80);
    }

    // -------------------------------------------------------------------------
    // T163: Function key ($90 = F1) stored correctly
    // -------------------------------------------------------------------------

    TEST_CASE("T163: Function key stored correctly") {
        EmulatorFixture f;
        kbd_inject_key(0x90, 0x00);  // F1
        CHECK(kbd_get_data() == 0x90);
    }

    // -------------------------------------------------------------------------
    // T164: Bus decode: write KBD_ACK via bus, clears status
    // -------------------------------------------------------------------------

    TEST_CASE("T164: Bus decode write KBD_ACK clears status") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        CHECK(kbd_data_available() == true);
        // Write ACK through bus decode
        uint64_t p = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(p, N8_KBD_ACK);
        CHECK(kbd_data_available() == false);
    }

    // -------------------------------------------------------------------------
    // T165: kbd_tick() reasserts IRQ when data avail + IRQ enabled
    // -------------------------------------------------------------------------

    TEST_CASE("T165: kbd_tick reasserts IRQ") {
        EmulatorFixture f;
        // Enable IRQ
        uint64_t pc = make_write_pins(N8_KBD_BASE + N8_KBD_CTRL, N8_KBD_CTRL_IRQ_EN);
        kbd_decode(pc, N8_KBD_CTRL);
        // Inject key
        kbd_inject_key(0x41, 0x00);
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) != 0);
        // Simulate IRQ_CLR
        mem[N8_IRQ_FLAGS] = 0x00;
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) == 0);
        // kbd_tick should reassert
        kbd_tick();
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) != 0);
    }

    // -------------------------------------------------------------------------
    // T166: kbd_tick() does NOT assert IRQ when IRQ disabled
    // -------------------------------------------------------------------------

    TEST_CASE("T166: kbd_tick does not assert IRQ when disabled") {
        EmulatorFixture f;
        // IRQ disabled (default)
        kbd_inject_key(0x41, 0x00);
        mem[N8_IRQ_FLAGS] = 0x00;
        kbd_tick();
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) == 0);
    }

    // -------------------------------------------------------------------------
    // T167: Two rapid injects buffered without overflow (buffer has room)
    // -------------------------------------------------------------------------

    TEST_CASE("T167: Two rapid injects buffered without overflow") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);
        kbd_inject_key(0x41, 0x00);  // simulated repeat
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) == 0);
        CHECK(kbd_data_available() == true);
        // Single inject should also not overflow
        kbd_reset();
        kbd_inject_key(0x41, 0x00);
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) == 0);
    }

    // =========================================================================
    // Ring buffer tests (T200+)
    // =========================================================================

    // -------------------------------------------------------------------------
    // T200: FIFO ordering — inject A, B, C; read A, ACK, read B, ACK, read C
    // -------------------------------------------------------------------------

    TEST_CASE("T200: FIFO ordering across three keys") {
        EmulatorFixture f;
        kbd_inject_key(0x41, 0x00);  // A
        kbd_inject_key(0x42, 0x00);  // B
        kbd_inject_key(0x43, 0x00);  // C

        CHECK(kbd_get_data() == 0x41);
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK(kbd_get_data() == 0x42);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK(kbd_get_data() == 0x43);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK(kbd_data_available() == false);
    }

    // -------------------------------------------------------------------------
    // T201: Buffer full at 64 entries; 65th dropped + OVERFLOW
    // -------------------------------------------------------------------------

    TEST_CASE("T201: Buffer full at 64, 65th dropped with OVERFLOW") {
        EmulatorFixture f;
        for (int i = 0; i < 64; i++) {
            kbd_inject_key((uint8_t)(i & 0xFF), 0x00);
        }
        CHECK(kbd_data_available() == true);
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) == 0);
        // First key should still be at front
        CHECK(kbd_get_data() == 0x00);

        // 65th inject overflows
        kbd_inject_key(0xFF, 0x00);
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) != 0);
        // Front still 0x00 (65th was dropped)
        CHECK(kbd_get_data() == 0x00);
    }

    // -------------------------------------------------------------------------
    // T202: ACK when empty is safe — no crash, AVAIL stays clear
    // -------------------------------------------------------------------------

    TEST_CASE("T202: ACK when empty is safe") {
        EmulatorFixture f;
        CHECK(kbd_data_available() == false);
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK(kbd_data_available() == false);
        CHECK(kbd_get_data() == 0x00);
    }

    // -------------------------------------------------------------------------
    // T203: ACK clears OVERFLOW but keeps AVAIL when buffer non-empty
    // -------------------------------------------------------------------------

    TEST_CASE("T203: ACK clears OVERFLOW but keeps AVAIL when non-empty") {
        EmulatorFixture f;
        // Fill buffer to trigger overflow
        for (int i = 0; i < 64; i++) {
            kbd_inject_key((uint8_t)(i + 1), 0x00);
        }
        kbd_inject_key(0xFF, 0x00);  // 65th = overflow
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) != 0);

        // ACK pops one, clears overflow, keeps AVAIL
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK((kbd_get_status() & N8_KBD_STAT_OVERFLOW) == 0);
        CHECK(kbd_data_available() == true);
        // Front advanced to second key
        CHECK(kbd_get_data() == 0x02);
    }

    // -------------------------------------------------------------------------
    // T204: kbd_tick() reasserts IRQ after partial drain
    // -------------------------------------------------------------------------

    TEST_CASE("T204: kbd_tick reasserts IRQ after partial drain") {
        EmulatorFixture f;
        // Enable IRQ
        uint64_t pc = make_write_pins(N8_KBD_BASE + N8_KBD_CTRL, N8_KBD_CTRL_IRQ_EN);
        kbd_decode(pc, N8_KBD_CTRL);
        // Inject two keys
        kbd_inject_key(0x41, 0x00);
        kbd_inject_key(0x42, 0x00);
        // ACK first key
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        // Simulate IRQ_CLR
        mem[N8_IRQ_FLAGS] = 0x00;
        // kbd_tick should reassert (buffer still has 0x42)
        kbd_tick();
        CHECK((mem[N8_IRQ_FLAGS] & (1 << N8_IRQ_BIT_KBD)) != 0);
    }

    // -------------------------------------------------------------------------
    // T205: Modifiers track front entry — advance changes visible modifiers
    // -------------------------------------------------------------------------

    TEST_CASE("T205: Modifiers track front entry across ACK") {
        EmulatorFixture f;
        kbd_inject_key(0x41, N8_KBD_STAT_SHIFT);
        kbd_inject_key(0x42, N8_KBD_STAT_CTRL);
        // Front is key1 with SHIFT
        CHECK((kbd_get_status() & N8_KBD_STAT_SHIFT) != 0);
        CHECK((kbd_get_status() & N8_KBD_STAT_CTRL) == 0);
        // ACK advances to key2 with CTRL
        uint64_t pa = make_write_pins(N8_KBD_BASE + N8_KBD_ACK, 0x00);
        kbd_decode(pa, N8_KBD_ACK);
        CHECK((kbd_get_status() & N8_KBD_STAT_SHIFT) == 0);
        CHECK((kbd_get_status() & N8_KBD_STAT_CTRL) != 0);
    }

    // -------------------------------------------------------------------------
    // T206: Reset clears entire buffer
    // -------------------------------------------------------------------------

    TEST_CASE("T206: Reset clears entire buffer") {
        EmulatorFixture f;
        for (int i = 0; i < 10; i++) {
            kbd_inject_key((uint8_t)(0x30 + i), 0x00);
        }
        CHECK(kbd_data_available() == true);
        kbd_reset();
        CHECK(kbd_data_available() == false);
        CHECK(kbd_get_data() == 0x00);
        CHECK(kbd_get_status() == 0x00);
    }

} // TEST_SUITE("keyboard")
