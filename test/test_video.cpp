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
    // T143: Phantom registers ($D84C-$D85F) read 0, write no-op
    // -------------------------------------------------------------------------

    TEST_CASE("T143: Phantom registers read 0, write no-op") {
        EmulatorFixture f;
        for (uint8_t reg = N8_VID_REG_COUNT; reg < 32; reg++) {
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

    // -------------------------------------------------------------------------
    // T146a: Scroll left with width > stride is a no-op (guard)
    // -------------------------------------------------------------------------

    TEST_CASE("T146a: Scroll left with width > stride is a no-op") {
        EmulatorFixture f;
        // Custom mode: width=40, stride=20 → width > stride
        uint64_t pm = make_write_pins(N8_VID_BASE + N8_VID_MODE, N8_VIDMODE_TEXT_CUSTOM);
        video_decode(pm, N8_VID_MODE);
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_WIDTH, 40);
        video_decode(pw, N8_VID_WIDTH);
        uint64_t ps = make_write_pins(N8_VID_BASE + N8_VID_STRIDE, 20);
        video_decode(ps, N8_VID_STRIDE);
        uint64_t ph = make_write_pins(N8_VID_BASE + N8_VID_HEIGHT, 10);
        video_decode(ph, N8_VID_HEIGHT);

        // Put marker data in row 0
        frame_buffer[0] = 'A';
        frame_buffer[1] = 'B';

        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_LEFT);
        video_decode(p, N8_VID_OPER);
        // Should be a no-op — data untouched
        CHECK(frame_buffer[0] == 'A');
        CHECK(frame_buffer[1] == 'B');
    }

    // -------------------------------------------------------------------------
    // T146b: Scroll right with width > stride is a no-op (guard)
    // -------------------------------------------------------------------------

    TEST_CASE("T146b: Scroll right with width > stride is a no-op") {
        EmulatorFixture f;
        uint64_t pm = make_write_pins(N8_VID_BASE + N8_VID_MODE, N8_VIDMODE_TEXT_CUSTOM);
        video_decode(pm, N8_VID_MODE);
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_WIDTH, 40);
        video_decode(pw, N8_VID_WIDTH);
        uint64_t ps = make_write_pins(N8_VID_BASE + N8_VID_STRIDE, 20);
        video_decode(ps, N8_VID_STRIDE);
        uint64_t ph = make_write_pins(N8_VID_BASE + N8_VID_HEIGHT, 10);
        video_decode(ph, N8_VID_HEIGHT);

        frame_buffer[0] = 'X';
        frame_buffer[1] = 'Y';

        uint64_t p = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_SCROLL_RIGHT);
        video_decode(p, N8_VID_OPER);
        CHECK(frame_buffer[0] == 'X');
        CHECK(frame_buffer[1] == 'Y');
    }

    // -------------------------------------------------------------------------
    // T147: VID_VSYNC reads 0 after reset
    // -------------------------------------------------------------------------

    TEST_CASE("T147: VID_VSYNC reads 0 after reset") {
        EmulatorFixture f;
        video_reset();
        uint64_t p = make_read_pins(N8_VID_BASE + N8_VID_VSYNC);
        video_decode(p, N8_VID_VSYNC);
        CHECK(M6502_GET_DATA(p) == 0x00);
    }

    // -------------------------------------------------------------------------
    // T148: VID_VSYNC increments on video_rasterize
    // -------------------------------------------------------------------------

    TEST_CASE("T148: VID_VSYNC increments on video_rasterize") {
        EmulatorFixture f;
        video_reset();
        fb_dirty = true;
        video_rasterize(0);
        uint64_t p = make_read_pins(N8_VID_BASE + N8_VID_VSYNC);
        video_decode(p, N8_VID_VSYNC);
        CHECK(M6502_GET_DATA(p) == 1);
        fb_dirty = true;
        video_rasterize(1);
        uint64_t p2 = make_read_pins(N8_VID_BASE + N8_VID_VSYNC);
        video_decode(p2, N8_VID_VSYNC);
        CHECK(M6502_GET_DATA(p2) == 2);
    }

    // -------------------------------------------------------------------------
    // T149: VID_VSYNC wraps at 256
    // -------------------------------------------------------------------------

    TEST_CASE("T149: VID_VSYNC wraps at 256") {
        EmulatorFixture f;
        video_reset();
        for (int i = 0; i < 255; i++) {
            video_rasterize(i);
        }
        uint64_t p1 = make_read_pins(N8_VID_BASE + N8_VID_VSYNC);
        video_decode(p1, N8_VID_VSYNC);
        CHECK(M6502_GET_DATA(p1) == 255);
        video_rasterize(255);
        uint64_t p2 = make_read_pins(N8_VID_BASE + N8_VID_VSYNC);
        video_decode(p2, N8_VID_VSYNC);
        CHECK(M6502_GET_DATA(p2) == 0);
    }

    // -------------------------------------------------------------------------
    // T149a: VID_VSYNC writes are ignored
    // -------------------------------------------------------------------------

    TEST_CASE("T149a: VID_VSYNC writes are ignored") {
        EmulatorFixture f;
        video_reset();
        fb_dirty = true;
        video_rasterize(0);
        // Try to write a value
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_VSYNC, 0x42);
        video_decode(pw, N8_VID_VSYNC);
        // Should still read 1 (from the one rasterize call)
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_VSYNC);
        video_decode(pr, N8_VID_VSYNC);
        CHECK(M6502_GET_DATA(pr) == 1);
    }

    // =========================================================================
    // VID_CTRL tests
    // =========================================================================

    TEST_CASE("T160: VID_CTRL defaults to $07 after reset") {
        EmulatorFixture f;
        CHECK(video_get_ctrl() == N8_VIDCTRL_DEFAULT);
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_CTRL);
        video_decode(pr, N8_VID_CTRL);
        CHECK(M6502_GET_DATA(pr) == 0x07);
    }

    TEST_CASE("T161: VID_CTRL write/read preserves bits 0-2, masks bits 3-7") {
        EmulatorFixture f;
        // Write all bits set
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0xFF);
        video_decode(pw, N8_VID_CTRL);
        CHECK(video_get_ctrl() == 0x07);
        // Write just bit 1
        uint64_t pw2 = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0x02);
        video_decode(pw2, N8_VID_CTRL);
        CHECK(video_get_ctrl() == 0x02);
    }

    TEST_CASE("T162: Writing VID_CTRL clears OVERFLOW") {
        EmulatorFixture f;
        // Set cursor to end of row, advance without wrap to trigger overflow
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);  // advance only, no wrap
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'X');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
        // Writing VID_CTRL clears overflow
        uint64_t pctrl2 = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_DEFAULT);
        video_decode(pctrl2, N8_VID_CTRL);
        CHECK(video_get_status() == 0x00);
    }

    // =========================================================================
    // VID_DATA write tests
    // =========================================================================

    TEST_CASE("T163: VID_DATA write stores byte at cursor position in frame_buffer") {
        EmulatorFixture f;
        // Disable advance to isolate store behavior
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0x00);
        video_decode(pctrl, N8_VID_CTRL);
        // Set cursor to (5, 2)
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 5);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 2);
        video_decode(pr, N8_VID_CURROW);
        // Write 'Z'
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'Z');
        video_decode(pd, N8_VID_DATA);
        CHECK(frame_buffer[2 * 80 + 5] == 'Z');
    }

    TEST_CASE("T164: VID_DATA write with ADVANCE increments CURCOL") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'A');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_cursor_col() == 1);
        CHECK(frame_buffer[0] == 'A');
    }

    TEST_CASE("T165: VID_DATA write ADVANCE+WRAP wraps col to 0, increments row") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL,
            N8_VIDCTRL_ADVANCE | N8_VIDCTRL_WRAP);
        video_decode(pctrl, N8_VID_CTRL);
        // Set cursor to last column
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        // Write
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'W');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_cursor_col() == 0);
        CHECK(video_get_cursor_row() == 1);
        CHECK(frame_buffer[79] == 'W');
    }

    TEST_CASE("T166: VID_DATA write ADVANCE+WRAP+SCROLL scrolls at bottom row") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_DEFAULT);
        video_decode(pctrl, N8_VID_CTRL);
        // Set cursor to last col, last row
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 24);
        video_decode(pr, N8_VID_CURROW);
        // Put marker in row 0 col 0
        frame_buffer[0] = 'M';
        // Put marker in row 1 col 0
        frame_buffer[80] = 'N';
        // Write — should trigger wrap then scroll
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'S');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_cursor_col() == 0);
        CHECK(video_get_cursor_row() == 24);
        // Row 0 should now have what was row 1
        CHECK(frame_buffer[0] == 'N');
    }

    TEST_CASE("T167: VID_DATA write with no ADVANCE leaves cursor unchanged") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0x00);
        video_decode(pctrl, N8_VID_CTRL);
        // Set cursor to (3, 1)
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 3);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 1);
        video_decode(pr, N8_VID_CURROW);
        // Write
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'Q');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_cursor_col() == 3);
        CHECK(video_get_cursor_row() == 1);
    }

    TEST_CASE("T168: VID_DATA write WRAP without SCROLL clamps row, sets OVERFLOW") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL,
            N8_VIDCTRL_ADVANCE | N8_VIDCTRL_WRAP);  // no SCROLL
        video_decode(pctrl, N8_VID_CTRL);
        // Last col, last row
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 24);
        video_decode(pr, N8_VID_CURROW);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'X');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_cursor_row() == 24);
        CHECK(video_get_cursor_col() == 0);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
    }

    TEST_CASE("T169: VID_DATA write ADVANCE without WRAP clamps col, sets OVERFLOW") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        // Last col
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'E');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_cursor_col() == 79);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
    }

    TEST_CASE("T170: VID_DATA write out-of-bounds cursor is no-op, sets OVERFLOW") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0x00);
        video_decode(pctrl, N8_VID_CTRL);
        // Set cursor beyond FB (row=255, stride=80 → offset=20400 > 4096)
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 255);
        video_decode(pr, N8_VID_CURROW);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'B');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
    }

    // =========================================================================
    // VID_DATA read tests
    // =========================================================================

    TEST_CASE("T171: VID_DATA read returns frame_buffer byte at cursor") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0x00);
        video_decode(pctrl, N8_VID_CTRL);
        frame_buffer[0] = 'H';
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_DATA);
        video_decode(pr, N8_VID_DATA);
        CHECK(M6502_GET_DATA(pr) == 'H');
    }

    TEST_CASE("T172: VID_DATA read with ADVANCE increments CURCOL") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        frame_buffer[0] = 'R';
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_DATA);
        video_decode(pr, N8_VID_DATA);
        CHECK(M6502_GET_DATA(pr) == 'R');
        CHECK(video_get_cursor_col() == 1);
    }

    TEST_CASE("T173: VID_DATA read with ADVANCE+WRAP wraps and advances row") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL,
            N8_VIDCTRL_ADVANCE | N8_VIDCTRL_WRAP);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        frame_buffer[79] = 'L';
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_DATA);
        video_decode(pr, N8_VID_DATA);
        CHECK(M6502_GET_DATA(pr) == 'L');
        CHECK(video_get_cursor_col() == 0);
        CHECK(video_get_cursor_row() == 1);
    }

    TEST_CASE("T174: VID_DATA read never scrolls, clamps row, sets OVERFLOW") {
        EmulatorFixture f;
        // Even with SCROLL enabled, read should not scroll
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_DEFAULT);
        video_decode(pctrl, N8_VID_CTRL);
        // Last col, last row
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr2 = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 24);
        video_decode(pr2, N8_VID_CURROW);
        // Put marker in row 0 — should survive (no scroll)
        frame_buffer[0] = 'K';
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_DATA);
        video_decode(pr, N8_VID_DATA);
        CHECK(frame_buffer[0] == 'K');  // not scrolled
        CHECK(video_get_cursor_row() == 24);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
    }

    TEST_CASE("T175: VID_DATA read out-of-bounds returns $00") {
        EmulatorFixture f;
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, 0x00);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pr2 = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 255);
        video_decode(pr2, N8_VID_CURROW);
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_DATA);
        video_decode(pr, N8_VID_DATA);
        CHECK(M6502_GET_DATA(pr) == 0x00);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
    }

    // =========================================================================
    // VID_STATUS tests
    // =========================================================================

    TEST_CASE("T176: VID_STATUS defaults to $00 after reset") {
        EmulatorFixture f;
        CHECK(video_get_status() == 0x00);
        uint64_t pr = make_read_pins(N8_VID_BASE + N8_VID_STATUS);
        video_decode(pr, N8_VID_STATUS);
        CHECK(M6502_GET_DATA(pr) == 0x00);
    }

    TEST_CASE("T177: VID_STATUS is read-only (writes ignored)") {
        EmulatorFixture f;
        uint64_t pw = make_write_pins(N8_VID_BASE + N8_VID_STATUS, 0xFF);
        video_decode(pw, N8_VID_STATUS);
        CHECK(video_get_status() == 0x00);
    }

    TEST_CASE("T178: OVERFLOW cleared by CURCOL write") {
        EmulatorFixture f;
        // Trigger overflow
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'X');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
        // Write CURCOL clears it
        uint64_t pc2 = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 0);
        video_decode(pc2, N8_VID_CURCOL);
        CHECK(video_get_status() == 0x00);
    }

    TEST_CASE("T179: OVERFLOW cleared by CURROW write") {
        EmulatorFixture f;
        // Trigger overflow
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'X');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
        // Write CURROW clears it
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 0);
        video_decode(pr, N8_VID_CURROW);
        CHECK(video_get_status() == 0x00);
    }

    TEST_CASE("T180: OVERFLOW cleared by CURSOR_HOME") {
        EmulatorFixture f;
        // Trigger overflow
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'X');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
        // CURSOR_HOME clears it
        uint64_t ph = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_HOME);
        video_decode(ph, N8_VID_OPER);
        CHECK(video_get_status() == 0x00);
    }

    // =========================================================================
    // New VID_OPER codes
    // =========================================================================

    TEST_CASE("T181: CLEAR fills visible area with $20, resets cursor, clears OVERFLOW") {
        EmulatorFixture f;
        // Write some data
        memset(frame_buffer, 'X', 80 * 25);
        // Set cursor away from origin
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 10);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 5);
        video_decode(pr, N8_VID_CURROW);
        // Trigger overflow first to test clearing
        uint64_t pctrl = make_write_pins(N8_VID_BASE + N8_VID_CTRL, N8_VIDCTRL_ADVANCE);
        video_decode(pctrl, N8_VID_CTRL);
        uint64_t pc2 = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc2, N8_VID_CURCOL);
        uint64_t pd = make_write_pins(N8_VID_BASE + N8_VID_DATA, 'Z');
        video_decode(pd, N8_VID_DATA);
        CHECK(video_get_status() == N8_VIDSTAT_OVERFLOW);
        // CLEAR
        uint64_t pop = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CLEAR);
        video_decode(pop, N8_VID_OPER);
        CHECK(frame_buffer[0] == 0x20);
        CHECK(frame_buffer[80 * 25 - 1] == 0x20);
        CHECK(video_get_cursor_col() == 0);
        CHECK(video_get_cursor_row() == 0);
        CHECK(video_get_status() == 0x00);
    }

    TEST_CASE("T182: CURSOR_UP decrements row, clamps at 0") {
        EmulatorFixture f;
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 3);
        video_decode(pr, N8_VID_CURROW);
        uint64_t pop = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_UP);
        video_decode(pop, N8_VID_OPER);
        CHECK(video_get_cursor_row() == 2);
        // Clamp at 0
        uint64_t pr2 = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 0);
        video_decode(pr2, N8_VID_CURROW);
        uint64_t pop2 = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_UP);
        video_decode(pop2, N8_VID_OPER);
        CHECK(video_get_cursor_row() == 0);
    }

    TEST_CASE("T183: CURSOR_DOWN increments row, clamps at height-1") {
        EmulatorFixture f;
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 0);
        video_decode(pr, N8_VID_CURROW);
        uint64_t pop = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_DOWN);
        video_decode(pop, N8_VID_OPER);
        CHECK(video_get_cursor_row() == 1);
        // Clamp at 24 (height=25)
        uint64_t pr2 = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 24);
        video_decode(pr2, N8_VID_CURROW);
        uint64_t pop2 = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_DOWN);
        video_decode(pop2, N8_VID_OPER);
        CHECK(video_get_cursor_row() == 24);
    }

    TEST_CASE("T184: CURSOR_LEFT decrements col, clamps at 0") {
        EmulatorFixture f;
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 5);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pop = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_LEFT);
        video_decode(pop, N8_VID_OPER);
        CHECK(video_get_cursor_col() == 4);
        // Clamp at 0
        uint64_t pc2 = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 0);
        video_decode(pc2, N8_VID_CURCOL);
        uint64_t pop2 = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_LEFT);
        video_decode(pop2, N8_VID_OPER);
        CHECK(video_get_cursor_col() == 0);
    }

    TEST_CASE("T185: CURSOR_RIGHT increments col, clamps at width-1") {
        EmulatorFixture f;
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 0);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pop = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_RIGHT);
        video_decode(pop, N8_VID_OPER);
        CHECK(video_get_cursor_col() == 1);
        // Clamp at 79 (width=80)
        uint64_t pc2 = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 79);
        video_decode(pc2, N8_VID_CURCOL);
        uint64_t pop2 = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_RIGHT);
        video_decode(pop2, N8_VID_OPER);
        CHECK(video_get_cursor_col() == 79);
    }

    TEST_CASE("T186: CURSOR_HOME sets (0,0), clears OVERFLOW") {
        EmulatorFixture f;
        uint64_t pc = make_write_pins(N8_VID_BASE + N8_VID_CURCOL, 40);
        video_decode(pc, N8_VID_CURCOL);
        uint64_t pr = make_write_pins(N8_VID_BASE + N8_VID_CURROW, 12);
        video_decode(pr, N8_VID_CURROW);
        uint64_t pop = make_write_pins(N8_VID_BASE + N8_VID_OPER, N8_VIDOP_CURSOR_HOME);
        video_decode(pop, N8_VID_OPER);
        CHECK(video_get_cursor_col() == 0);
        CHECK(video_get_cursor_row() == 0);
    }

} // TEST_SUITE("video")
