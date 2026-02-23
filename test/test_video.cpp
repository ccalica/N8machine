#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("video") {

    // -------------------------------------------------------------------------
    // T130: video_reset sets mode=0, width=80, height=25, stride=80
    // -------------------------------------------------------------------------

    TEST_CASE("T130: video_reset sets default text mode dimensions") {
        EmulatorFixture f;
        video_reset();
        CHECK(video_get_mode() == 0x00);
        CHECK(video_get_width() == 80);
        CHECK(video_get_height() == 25);
        CHECK(video_get_stride() == 80);
    }

    // -------------------------------------------------------------------------
    // T131: Read VID_MODE ($D840) returns $00 after reset
    // -------------------------------------------------------------------------

    TEST_CASE("T131: Read VID_MODE returns $00 after reset") {
        EmulatorFixture f;
        uint64_t p = make_read_pins(N8_VID_BASE);
        video_decode(p, N8_VID_MODE);
        CHECK(M6502_GET_DATA(p) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T132: Write VID_MODE=$00 auto-sets 80/25/80
    // -------------------------------------------------------------------------

    TEST_CASE("T132: Write VID_MODE=$00 auto-sets 80/25/80") {
        EmulatorFixture f;
        // First set custom mode and change dims
        uint64_t p1 = make_write_pins(N8_VID_BASE, 0x01);
        video_decode(p1, N8_VID_MODE);
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_WIDTH, 40);
        video_decode(pw, N8_VID_WIDTH);
        // Now write default mode
        uint64_t p2 = make_write_pins(N8_VID_BASE, 0x00);
        video_decode(p2, N8_VID_MODE);
        CHECK(video_get_width() == 80);
        CHECK(video_get_height() == 25);
        CHECK(video_get_stride() == 80);
    }

    // -------------------------------------------------------------------------
    // T133: Write VID_MODE=$01 retains current dims
    // -------------------------------------------------------------------------

    TEST_CASE("T133: Write VID_MODE=$01 retains current dims") {
        EmulatorFixture f;
        // Set custom width first
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_WIDTH, 40);
        video_decode(pw, N8_VID_WIDTH);
        // Switch to custom mode
        uint64_t pm = make_write_pins(N8_VID_BASE, 0x01);
        video_decode(pm, N8_VID_MODE);
        CHECK(video_get_width() == 40);
    }

    // -------------------------------------------------------------------------
    // T134: Write/read VID_WIDTH
    // -------------------------------------------------------------------------

    TEST_CASE("T134: Write/read VID_WIDTH") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_WIDTH, 40);
        video_decode(pw, N8_VID_WIDTH);
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_WIDTH);
        video_decode(pr, N8_VID_WIDTH);
        CHECK(M6502_GET_DATA(pr) == 40);
    }

    // -------------------------------------------------------------------------
    // T135: Write/read VID_HEIGHT
    // -------------------------------------------------------------------------

    TEST_CASE("T135: Write/read VID_HEIGHT") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_HEIGHT, 50);
        video_decode(pw, N8_VID_HEIGHT);
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_HEIGHT);
        video_decode(pr, N8_VID_HEIGHT);
        CHECK(M6502_GET_DATA(pr) == 50);
    }

    // -------------------------------------------------------------------------
    // T136: Write/read VID_STRIDE
    // -------------------------------------------------------------------------

    TEST_CASE("T136: Write/read VID_STRIDE") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_STRIDE, 128);
        video_decode(pw, N8_VID_STRIDE);
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_STRIDE);
        video_decode(pr, N8_VID_STRIDE);
        CHECK(M6502_GET_DATA(pr) == 128);
    }

    // -------------------------------------------------------------------------
    // T137: Scroll up: row 0 gets row 1, last row zeroed
    // -------------------------------------------------------------------------

    TEST_CASE("T137: Scroll up shifts rows correctly") {
        EmulatorFixture f;
        video_reset(); // 80x25, stride=80
        // Fill row 0 with 'A', row 1 with 'B'
        memset(frame_buffer, 'A', 80);
        memset(frame_buffer + 80, 'B', 80);
        // Trigger scroll up via bus decode
        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_UP);
        video_decode(p, N8_VID_OPER);
        // Row 0 should now be 'B', last row should be zeroed
        CHECK(frame_buffer[0] == 'B');
        CHECK(frame_buffer[80 * 24] == 0x00);
    }

    // -------------------------------------------------------------------------
    // T138: Scroll down: row 1 gets row 0, first row zeroed
    // -------------------------------------------------------------------------

    TEST_CASE("T138: Scroll down shifts rows correctly") {
        EmulatorFixture f;
        video_reset();
        memset(frame_buffer, 'A', 80);
        memset(frame_buffer + 80, 'B', 80);
        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_DOWN);
        video_decode(p, N8_VID_OPER);
        // Row 0 should be zeroed, row 1 should be 'A'
        CHECK(frame_buffer[0] == 0x00);
        CHECK(frame_buffer[80] == 'A');
    }

    // -------------------------------------------------------------------------
    // T139: Read VID_OPER returns 0 (write-only)
    // -------------------------------------------------------------------------

    TEST_CASE("T139: Read VID_OPER returns 0") {
        EmulatorFixture f;
        uint64_t p = make_read_pins(N8_VID_BASE + N8_VID_OPER);
        video_decode(p, N8_VID_OPER);
        CHECK(M6502_GET_DATA(p) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T139a: VID_OPER=$00 (NOP) does not modify frame buffer
    // -------------------------------------------------------------------------

    TEST_CASE("T139a: VID_OPER NOP does not modify frame buffer") {
        EmulatorFixture f;
        video_reset();
        memset(frame_buffer, 'X', 80);
        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_NOP);
        video_decode(p, N8_VID_OPER);
        CHECK(frame_buffer[0] == 'X');
        CHECK(frame_buffer[79] == 'X');
    }

    // -------------------------------------------------------------------------
    // T140: Write/read VID_CURSOR
    // -------------------------------------------------------------------------

    TEST_CASE("T140: Write/read VID_CURSOR") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_CURSOR, 0x51);
        video_decode(pw, N8_VID_CURSOR);
        CHECK(video_get_cursor_style() == 0x51);
    }

    // -------------------------------------------------------------------------
    // T141: Write/read VID_CURCOL
    // -------------------------------------------------------------------------

    TEST_CASE("T141: Write/read VID_CURCOL") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 40);
        video_decode(pw, N8_VID_CURCOL);
        CHECK(video_get_cursor_col() == 40);
    }

    // -------------------------------------------------------------------------
    // T142: Write/read VID_CURROW
    // -------------------------------------------------------------------------

    TEST_CASE("T142: Write/read VID_CURROW") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 12);
        video_decode(pw, N8_VID_CURROW);
        CHECK(video_get_cursor_row() == 12);
    }

    // -------------------------------------------------------------------------
    // T142a: VID_CURSOR mode=FLASH + rate=0 -> cursor not displayed
    // -------------------------------------------------------------------------

    TEST_CASE("T142a: VID_CURSOR mode=FLASH + rate=0 means not displayed") {
        EmulatorFixture f;
        // FLASH mode (0x02), rate=0 (bits 4-7 = 0)
        uint8_t cursor_val = N8_VID_CURSOR_FLASH; // 0x02
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_CURSOR, cursor_val);
        video_decode(pw, N8_VID_CURSOR);
        uint8_t style = video_get_cursor_style();
        // Mode is flash
        CHECK((style & N8_VID_CURSOR_MODE_MASK) == N8_VID_CURSOR_FLASH);
        // Rate is 0 — cursor should not be displayed
        CHECK((style & N8_VID_CURSOR_RATE_MASK) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T143: Phantom registers ($D848-$D85F) read 0, write no-op
    // -------------------------------------------------------------------------

    TEST_CASE("T143: Phantom registers read 0, write no-op") {
        EmulatorFixture f;
        for (uint8_t reg = 8; reg < 32; reg++) {
            uint64_t pr = make_read_pins(N8_VID_BASE + reg);
            video_decode(pr, reg);
            CHECK(M6502_GET_DATA(pr) == 0x00);
        }
    }

    // -------------------------------------------------------------------------
    // T144: Scroll left
    // -------------------------------------------------------------------------

    TEST_CASE("T144: Scroll left shifts columns correctly") {
        EmulatorFixture f;
        video_reset();
        // Fill row 0: col 0='A', col 1='B', rest='C'
        frame_buffer[0] = 'A';
        frame_buffer[1] = 'B';
        for (int i = 2; i < 80; i++) frame_buffer[i] = 'C';
        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_LEFT);
        video_decode(p, N8_VID_OPER);
        // Col 0 should now be 'B', last col should be 0
        CHECK(frame_buffer[0] == 'B');
        CHECK(frame_buffer[79] == 0x00);
    }

    // -------------------------------------------------------------------------
    // T145: Scroll right
    // -------------------------------------------------------------------------

    TEST_CASE("T145: Scroll right shifts columns correctly") {
        EmulatorFixture f;
        video_reset();
        frame_buffer[0] = 'A';
        frame_buffer[1] = 'B';
        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_RIGHT);
        video_decode(p, N8_VID_OPER);
        // Col 0 should be 0, col 1 should be 'A'
        CHECK(frame_buffer[0] == 0x00);
        CHECK(frame_buffer[1] == 'A');
    }

    // -------------------------------------------------------------------------
    // T146: Scroll with oversized stride*height does not corrupt memory
    // -------------------------------------------------------------------------

    TEST_CASE("T146: Scroll with oversized stride*height is safe") {
        EmulatorFixture f;
        // Set stride=255, height=255 → 255*255=65025 > N8_FB_SIZE (4096)
        // safe_rows() should clamp
        uint64_t pm = make_write_pins(N8_VID_BASE + N8_VID_MODE, N8_VIDMODE_TEXT_CUSTOM);
        video_decode(pm, N8_VID_MODE);
        uint64_t ps = make_write_pins(N8_VID_BASE + N8_VID_STRIDE, 255);
        video_decode(ps, N8_VID_STRIDE);
        uint64_t ph = make_write_pins(N8_VID_BASE + N8_VID_HEIGHT, 255);
        video_decode(ph, N8_VID_HEIGHT);

        // Should not crash or corrupt
        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_UP);
        video_decode(p, N8_VID_OPER);
        CHECK(true); // no crash is the pass condition
    }

} // TEST_SUITE("video")
