#include "doctest.h"
#include "test_helpers.h"

TEST_SUITE("labels") {

    // -------------------------------------------------------------------------
    // T90: Add and get
    // -------------------------------------------------------------------------

    TEST_CASE("T90: Add and get -- add 'main' at 0xD000, get returns list containing 'main'") {
        emu_labels_clear();
        emu_labels_add(0xD000, (char*)"main");
        auto labels = emu_labels_get(0xD000);
        bool found = false;
        for (const auto& l : labels) {
            if (l == "main") { found = true; break; }
        }
        CHECK(found);
    }

    // -------------------------------------------------------------------------
    // T91: Get empty
    // -------------------------------------------------------------------------

    TEST_CASE("T91: Get empty -- get(0x1234) on fresh store returns empty list") {
        emu_labels_clear();
        auto labels = emu_labels_get(0x1234);
        CHECK(labels.empty());
    }

    // -------------------------------------------------------------------------
    // T92: Multiple labels same address
    // -------------------------------------------------------------------------

    TEST_CASE("T92: Multiple labels same addr -- add 'foo' and 'bar' at 0xD000, both in list") {
        emu_labels_clear();
        emu_labels_add(0xD000, (char*)"foo");
        emu_labels_add(0xD000, (char*)"bar");
        auto labels = emu_labels_get(0xD000);
        bool found_foo = false, found_bar = false;
        for (const auto& l : labels) {
            if (l == "foo") found_foo = true;
            if (l == "bar") found_bar = true;
        }
        CHECK(found_foo);
        CHECK(found_bar);
    }

    // -------------------------------------------------------------------------
    // T93: Clear
    // -------------------------------------------------------------------------

    TEST_CASE("T93: Clear -- add labels, clear, get returns empty list") {
        emu_labels_clear();
        emu_labels_add(0xD000, (char*)"main");
        emu_labels_add(0xD001, (char*)"loop");
        emu_labels_clear();
        CHECK(emu_labels_get(0xD000).empty());
        CHECK(emu_labels_get(0xD001).empty());
    }

    // -------------------------------------------------------------------------
    // T94: No duplicates
    // -------------------------------------------------------------------------

    TEST_CASE("T94: No duplicates -- add 'main' at 0xD000 twice, list has exactly one 'main'") {
        emu_labels_clear();
        emu_labels_add(0xD000, (char*)"main");
        emu_labels_add(0xD000, (char*)"main");
        auto labels = emu_labels_get(0xD000);
        int count = 0;
        for (const auto& l : labels) {
            if (l == "main") count++;
        }
        CHECK(count == 1);
    }

    // -------------------------------------------------------------------------
    // T94a: Console list output
    // -------------------------------------------------------------------------

    TEST_CASE("T94a: Console list output -- add labels, emu_labels_console_list writes to console buffer") {
        emu_labels_clear();
        stub_clear_console_buffer();
        emu_labels_add(0xD000, (char*)"main");
        emu_labels_add(0xD100, (char*)"irq");
        emu_labels_console_list();
        CHECK(!stub_get_console_buffer().empty());
    }

} // TEST_SUITE("labels")
