#include "doctest.h"
#include "test_helpers.h"
#include "emu_storage.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

// ---- Test fixture with temp storage directory ----

struct StorageFixture {
    EmulatorFixture f;
    char tmpdir[256];
    bool tmpdir_valid;

    StorageFixture() {
        strcpy(tmpdir, "/tmp/n8_storage_XXXXXX");
        tmpdir_valid = (mkdtemp(tmpdir) != NULL);
        if (tmpdir_valid) {
            char root[280];
            snprintf(root, sizeof(root), "%s/", tmpdir);
            storage_set_root_path(root);
            storage_init();
        }
    }

    ~StorageFixture() {
        if (tmpdir_valid) {
            // Clean up temp dir
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
            (void)system(cmd);
        }
    }

    void create_file(const char* name, const char* content) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", tmpdir, name);
        FILE* fp = fopen(path, "wb");
        if (fp) {
            fwrite(content, 1, strlen(content), fp);
            fclose(fp);
        }
    }

    void create_file_binary(const char* name, const uint8_t* data, size_t len) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", tmpdir, name);
        FILE* fp = fopen(path, "wb");
        if (fp) {
            fwrite(data, 1, len, fp);
            fclose(fp);
        }
    }

    void create_dir(const char* name) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", tmpdir, name);
        mkdir(path, 0755);
    }

    // Helper: write byte to storage register
    void write_reg(uint8_t reg, uint8_t val) {
        uint64_t p = make_write_pins(N8_STORAGE_BASE + reg, val);
        storage_decode(p, reg);
    }

    // Helper: read byte from storage register
    uint8_t read_reg(uint8_t reg) {
        uint64_t p = make_read_pins(N8_STORAGE_BASE + reg);
        storage_decode(p, reg);
        return M6502_GET_DATA(p);
    }

    // Helper: send command string (auto null-terminate)
    void send_command(const char* cmd) {
        // Select control channel
        write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        // Write command bytes
        for (const char* p = cmd; *p; p++) {
            write_reg(N8_DISK_DATA, (uint8_t)*p);
        }
        // Null terminate
        write_reg(N8_DISK_DATA, 0x00);
    }

    // Helper: check for command error
    // Note: assumes control channel is already selected (send_command does this)
    bool has_cmd_error() {
        return (read_reg(N8_DISK_ERROR) & N8_DISK_ERR_CMD) != 0;
    }

    // Helper: get command error code (reads DISK_DATA on control channel)
    uint8_t get_cmd_error() {
        return read_reg(N8_DISK_DATA);
    }

    // Helper: get command result (reads DISK_DATA on control channel)
    uint8_t get_cmd_result() {
        return read_reg(N8_DISK_DATA);
    }

    // Helper: read all data from a channel into a vector
    std::vector<uint8_t> read_channel(uint8_t chan_id) {
        std::vector<uint8_t> data;
        write_reg(N8_DISK_CHAN, chan_id);
        for (int guard = 0; guard < 100000; guard++) {
            uint8_t status = read_reg(N8_DISK_STATUS);
            uint8_t avail = status & N8_DISK_STAT_AVAIL;
            if (avail > 0) {
                for (int i = 0; i < avail; i++) {
                    data.push_back(read_reg(N8_DISK_DATA));
                }
            } else if (status & N8_DISK_STAT_EOF) {
                break;
            }
            // else busy — keep polling (won't happen in sync mode)
        }
        return data;
    }
};

TEST_SUITE("storage") {

    // =========================================================================
    // Register basics
    // =========================================================================

    TEST_CASE("register: DISK_CHAN write/read selects channel") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, 0x03);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_MASK) == 0x03);
    }

    TEST_CASE("register: DISK_CHAN active bit is 0 for unused channel") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, 0x00);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("register: DISK_CHAN write clears error") {
        StorageFixture s;
        // Generate an error: send invalid command
        s.send_command("XX");
        CHECK(s.has_cmd_error());  // error set
        // Re-selecting control channel clears error
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        CHECK((s.read_reg(N8_DISK_ERROR) & N8_DISK_ERR_CMD) == 0);
    }

    TEST_CASE("register: phantom registers return 0x00") {
        StorageFixture s;
        for (uint8_t reg = N8_DISK_REG_COUNT; reg < 0x1F; reg++) {
            CHECK(s.read_reg(reg) == 0x00);
        }
    }

    TEST_CASE("register: DISK_CTRL read returns 0x00") {
        StorageFixture s;
        CHECK(s.read_reg(N8_DISK_CTRL) == 0x00);
    }

    TEST_CASE("register: DISK_ERROR read 0x00 on init") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        CHECK(s.read_reg(N8_DISK_ERROR) == 0x00);
    }

    // =========================================================================
    // DISK_CTRL
    // =========================================================================

    TEST_CASE("DISK_CTRL bit 7: device reset closes all channels") {
        StorageFixture s;
        s.create_file("test.txt", "hello");

        s.send_command("OP,R,test.txt");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();

        // Verify channel is active
        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) != 0);

        // Device reset
        s.write_reg(N8_DISK_CTRL, N8_DISK_CTRL_DEVICE);

        // Channel should be inactive
        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("DISK_CTRL bit 0: parser reset clears state and response channels") {
        StorageFixture s;

        // Start a PWDIR and get response channel
        s.send_command("PD");
        uint8_t ch = s.get_cmd_result();
        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) != 0);

        // Parser reset should close response channels
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        s.write_reg(N8_DISK_CTRL, N8_DISK_CTRL_PARSER);

        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("DISK_CTRL bit 1: reset channel closes file") {
        StorageFixture s;
        s.create_file("test.txt", "hello");

        s.send_command("OP,R,test.txt");
        uint8_t ch = s.get_cmd_result();

        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) != 0);

        // Reset this channel
        s.write_reg(N8_DISK_CTRL, N8_DISK_CTRL_CHANNEL);

        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("DISK_CTRL bit 1 on closed channel: error $01") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, 0x00);
        s.write_reg(N8_DISK_CTRL, N8_DISK_CTRL_CHANNEL);
        CHECK(s.read_reg(N8_DISK_ERROR) == N8_DISK_IOE_NOT_OPEN);
    }

    // =========================================================================
    // PWDIR
    // =========================================================================

    TEST_CASE("PWDIR: returns / on init") {
        StorageFixture s;
        s.send_command("PD");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);
        CHECK(data.size() == 1);
        CHECK(data[0] == '/');
    }

    TEST_CASE("PWDIR: response channel auto-closes after read") {
        StorageFixture s;
        s.send_command("PD");
        uint8_t ch = s.get_cmd_result();
        s.read_channel(ch);

        // Channel should now be inactive
        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("PWDIR: bad syntax with args") {
        StorageFixture s;
        s.send_command("PD,foo");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_BAD_SYNTAX);
    }

    // =========================================================================
    // OPEN (read mode)
    // =========================================================================

    TEST_CASE("OPEN R: read file contents") {
        StorageFixture s;
        s.create_file("hello.txt", "Hello, World!");

        s.send_command("OP,R,hello.txt");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();

        auto data = s.read_channel(ch);
        std::string result(data.begin(), data.end());
        CHECK(result == "Hello, World!");
    }

    TEST_CASE("OPEN R: file not found") {
        StorageFixture s;
        s.send_command("OP,R,nonexistent.txt");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_FILE_NOT_FOUND);
    }

    TEST_CASE("OPEN R: double open returns error $02") {
        StorageFixture s;
        s.create_file("test.txt", "data");

        s.send_command("OP,R,test.txt");
        CHECK(!s.has_cmd_error());

        // Second open of same file
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        s.send_command("OP,R,test.txt");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_FILE_EXISTS);
    }

    TEST_CASE("OPEN W: returns bad arg (Phase 1)") {
        StorageFixture s;
        s.send_command("OP,W,test.txt");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_BAD_ARG);
    }

    TEST_CASE("OPEN R: name too long") {
        StorageFixture s;
        char cmd[128] = "OP,R,";
        memset(cmd + 5, 'A', 64);
        cmd[69] = '\0';
        s.send_command(cmd);
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_NAME_TOO_LONG);
    }

    TEST_CASE("OPEN R: STATUS bytes_avail saturates at 15") {
        StorageFixture s;
        // Create file with > 15 bytes
        char data[64];
        memset(data, 'X', sizeof(data));
        s.create_file_binary("big.txt", (uint8_t*)data, sizeof(data));

        s.send_command("OP,R,big.txt");
        uint8_t ch = s.get_cmd_result();

        s.write_reg(N8_DISK_CHAN, ch);
        uint8_t status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 15);
    }

    TEST_CASE("OPEN R: STATUS EOF lifecycle") {
        StorageFixture s;
        s.create_file("tiny.txt", "AB");

        s.send_command("OP,R,tiny.txt");
        uint8_t ch = s.get_cmd_result();
        s.write_reg(N8_DISK_CHAN, ch);

        // Should have data, not EOF
        uint8_t status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 2);
        CHECK((status & N8_DISK_STAT_EOF) == 0);

        // Read both bytes
        s.read_reg(N8_DISK_DATA);
        s.read_reg(N8_DISK_DATA);

        // Now should be EOF
        status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 0);
        CHECK((status & N8_DISK_STAT_EOF) != 0);
    }

    TEST_CASE("OPEN R: read past EOF sets error $03") {
        StorageFixture s;
        s.create_file("tiny.txt", "A");

        s.send_command("OP,R,tiny.txt");
        uint8_t ch = s.get_cmd_result();
        s.write_reg(N8_DISK_CHAN, ch);

        // Read the one byte
        s.read_reg(N8_DISK_DATA);
        // Read past EOF
        s.read_reg(N8_DISK_DATA);
        CHECK(s.read_reg(N8_DISK_ERROR) == N8_DISK_IOE_PAST_EOF);
    }

    // =========================================================================
    // CLOSE
    // =========================================================================

    TEST_CASE("CLOSE: closes open file channel") {
        StorageFixture s;
        s.create_file("test.txt", "data");

        s.send_command("OP,R,test.txt");
        uint8_t ch = s.get_cmd_result();

        // Close it
        char cmd[8];
        snprintf(cmd, sizeof(cmd), "CL,%X", ch);
        s.send_command(cmd);
        CHECK(!s.has_cmd_error());

        // Verify closed
        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("CLOSE: closing closed channel returns error $04") {
        StorageFixture s;
        s.send_command("CL,0");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_CHAN_NOT_OPEN);
    }

    TEST_CASE("CLOSE: allows reopening same file") {
        StorageFixture s;
        s.create_file("test.txt", "data");

        s.send_command("OP,R,test.txt");
        uint8_t ch = s.get_cmd_result();

        char cmd[8];
        snprintf(cmd, sizeof(cmd), "CL,%X", ch);
        s.send_command(cmd);

        // Should be able to reopen
        s.send_command("OP,R,test.txt");
        CHECK(!s.has_cmd_error());
    }

    // =========================================================================
    // LIST
    // =========================================================================

    TEST_CASE("LIST: empty directory") {
        StorageFixture s;
        s.send_command("LS");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);
        CHECK(data.empty());
    }

    TEST_CASE("LIST: files and directories") {
        StorageFixture s;
        s.create_file("alpha.txt", "aaa");
        s.create_file("beta.txt", "bb");
        s.create_dir("subdir");

        s.send_command("LS");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);

        // Parse response into entries
        std::string response(data.begin(), data.end());
        // Should start with D\tsubdir (directory first)
        CHECK(data[0] == 'D');
        CHECK(data[1] == '\t');

        // Count entries (newlines)
        int entries = 0;
        for (size_t i = 0; i < data.size(); i++) {
            if (data[i] == '\n') entries++;
        }
        CHECK(entries == 3);
    }

    TEST_CASE("LIST: auto-close after read") {
        StorageFixture s;
        s.create_file("test.txt", "data");
        s.send_command("LS");
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);
        CHECK(!data.empty());

        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    TEST_CASE("LIST: single file") {
        StorageFixture s;
        s.create_file("hello.txt", "Hello!");

        s.send_command("LS,hello.txt");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);

        // Should be: F\thello.txt\t<lo><hi>\n
        CHECK(data[0] == 'F');
        CHECK(data[1] == '\t');
    }

    TEST_CASE("LIST: nonexistent path") {
        StorageFixture s;
        s.send_command("LS,nonexistent");
        CHECK(s.has_cmd_error());
    }

    TEST_CASE("LIST: size field is LE uint16") {
        StorageFixture s;
        // Create file with known size (256 bytes = 0x0100)
        uint8_t buf[256];
        memset(buf, 'X', sizeof(buf));
        s.create_file_binary("sized.txt", buf, sizeof(buf));

        s.send_command("LS,sized.txt");
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);

        // Find second tab (after filename)
        int tabs = 0;
        size_t size_offset = 0;
        for (size_t i = 0; i < data.size(); i++) {
            if (data[i] == '\t') {
                tabs++;
                if (tabs == 2) { size_offset = i + 1; break; }
            }
        }
        CHECK(size_offset > 0);
        uint16_t size = data[size_offset] | (data[size_offset + 1] << 8);
        CHECK(size == 256);
    }

    // =========================================================================
    // Firmware-style read loop (one byte per STATUS check)
    // =========================================================================

    TEST_CASE("LIST: firmware-style one-byte-per-STATUS read loop") {
        StorageFixture s;
        s.create_file("test.txt", "data");

        s.send_command("LS");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();
        s.write_reg(N8_DISK_CHAN, ch);

        // Read one byte at a time, checking STATUS each iteration
        // (mimics firmware read loop)
        std::vector<uint8_t> data;
        for (int guard = 0; guard < 1000; guard++) {
            uint8_t status = s.read_reg(N8_DISK_STATUS);
            uint8_t avail = status & N8_DISK_STAT_AVAIL;
            if (avail > 0) {
                // Read exactly ONE byte (firmware style)
                data.push_back(s.read_reg(N8_DISK_DATA));
            } else if (status & N8_DISK_STAT_EOF) {
                break;
            }
            // else busy-wait
            if (guard == 999) FAIL("read loop did not terminate");
        }
        CHECK(!data.empty());

        // Channel should be auto-closed
        s.write_reg(N8_DISK_CHAN, ch);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) == 0);
    }

    // =========================================================================
    // Multi-line file read (storage/test.txt content)
    // =========================================================================

    TEST_CASE("OPEN R: read multi-line file matches byte-for-byte") {
        StorageFixture s;
        const char* content = "Foo\nBar\nBaz\n\n";
        s.create_file("test.txt", content);

        s.send_command("OP,R,test.txt");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();

        auto data = s.read_channel(ch);
        std::string result(data.begin(), data.end());
        CHECK(result == content);
        CHECK(data.size() == 13);
    }

    TEST_CASE("OPEN R: multi-line file STATUS tracks bytes correctly") {
        StorageFixture s;
        s.create_file("test.txt", "Foo\nBar\nBaz\n\n");

        s.send_command("OP,R,test.txt");
        uint8_t ch = s.get_cmd_result();
        s.write_reg(N8_DISK_CHAN, ch);

        // 13 bytes < 256 buffer, so all buffered, saturates at 15
        uint8_t status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 13);
        CHECK((status & N8_DISK_STAT_EOF) == 0);

        // Read 3 bytes ("Foo")
        CHECK(s.read_reg(N8_DISK_DATA) == 'F');
        CHECK(s.read_reg(N8_DISK_DATA) == 'o');
        CHECK(s.read_reg(N8_DISK_DATA) == 'o');

        // 10 bytes remaining
        status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 10);

        // Read the \n
        CHECK(s.read_reg(N8_DISK_DATA) == '\n');

        // Read "Bar\n"
        CHECK(s.read_reg(N8_DISK_DATA) == 'B');
        CHECK(s.read_reg(N8_DISK_DATA) == 'a');
        CHECK(s.read_reg(N8_DISK_DATA) == 'r');
        CHECK(s.read_reg(N8_DISK_DATA) == '\n');

        // 5 bytes remaining: "Baz\n\n"
        status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 5);

        // Drain remaining
        CHECK(s.read_reg(N8_DISK_DATA) == 'B');
        CHECK(s.read_reg(N8_DISK_DATA) == 'a');
        CHECK(s.read_reg(N8_DISK_DATA) == 'z');
        CHECK(s.read_reg(N8_DISK_DATA) == '\n');
        CHECK(s.read_reg(N8_DISK_DATA) == '\n');

        // EOF
        status = s.read_reg(N8_DISK_STATUS);
        CHECK((status & N8_DISK_STAT_AVAIL) == 0);
        CHECK((status & N8_DISK_STAT_EOF) != 0);
    }

    TEST_CASE("OPEN R: multi-line file via LIST shows correct size") {
        StorageFixture s;
        s.create_file("test.txt", "Foo\nBar\nBaz\n\n");

        s.send_command("LS,test.txt");
        CHECK(!s.has_cmd_error());
        uint8_t ch = s.get_cmd_result();
        auto data = s.read_channel(ch);

        // F\ttest.txt\t<0D><00>\n  (13 = 0x0D)
        CHECK(data[0] == 'F');
        // Find size bytes (after second tab)
        int tabs = 0;
        size_t size_off = 0;
        for (size_t i = 0; i < data.size(); i++) {
            if (data[i] == '\t') { tabs++; if (tabs == 2) { size_off = i + 1; break; } }
        }
        CHECK(size_off > 0);
        uint16_t size = data[size_off] | (data[size_off + 1] << 8);
        CHECK(size == 13);
    }

    // =========================================================================
    // Command parser edge cases
    // =========================================================================

    TEST_CASE("parser: double null is ignored") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        // Send "PD" + null + null
        s.write_reg(N8_DISK_DATA, 'P');
        s.write_reg(N8_DISK_DATA, 'D');
        s.write_reg(N8_DISK_DATA, 0x00);  // executes PD
        uint8_t ch1 = s.read_reg(N8_DISK_DATA);

        s.write_reg(N8_DISK_DATA, 0x00);  // should be ignored
        // Reading again should give 0x00 (no new result)
        uint8_t val = s.read_reg(N8_DISK_DATA);
        CHECK(val == 0x00);

        // ch1 should still be a valid response channel
        s.write_reg(N8_DISK_CHAN, ch1);
        CHECK((s.read_reg(N8_DISK_CHAN) & N8_DISK_CHAN_ACTIVE) != 0);
    }

    TEST_CASE("parser: command buffer overflow sets error $06") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);

        // Write 257 bytes without null
        for (int i = 0; i < 257; i++) {
            s.write_reg(N8_DISK_DATA, 'A');
        }

        CHECK(s.read_reg(N8_DISK_ERROR) == N8_DISK_IOE_CMD_OVERFLOW);
    }

    TEST_CASE("parser: invalid command returns bad syntax") {
        StorageFixture s;
        s.send_command("XX");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_BAD_SYNTAX);
    }

    TEST_CASE("parser: too-short command returns bad syntax") {
        StorageFixture s;
        s.send_command("X");
        CHECK(s.has_cmd_error());
        CHECK(s.get_cmd_error() == N8_DISK_CE_BAD_SYNTAX);
    }

    // =========================================================================
    // Data channel I/O errors
    // =========================================================================

    TEST_CASE("read on closed channel sets error $01") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, 0x00);
        s.read_reg(N8_DISK_DATA);
        CHECK(s.read_reg(N8_DISK_ERROR) == N8_DISK_IOE_NOT_OPEN);
    }

    TEST_CASE("write on read-only channel sets error $04") {
        StorageFixture s;
        s.create_file("test.txt", "data");

        s.send_command("OP,R,test.txt");
        uint8_t ch = s.get_cmd_result();

        s.write_reg(N8_DISK_CHAN, ch);
        s.write_reg(N8_DISK_DATA, 0x41);
        CHECK(s.read_reg(N8_DISK_ERROR) == N8_DISK_IOE_PERMISSION);
    }

    // =========================================================================
    // GDB bridge accessors
    // =========================================================================

    TEST_CASE("GDB: storage_get_chan returns current channel") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, 0x05);
        CHECK((storage_get_chan() & N8_DISK_CHAN_MASK) == 0x05);
    }

    TEST_CASE("GDB: storage_get_error returns 0 on init") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, N8_DISK_CONTROL_CHAN);
        CHECK(storage_get_error() == 0x00);
    }

    TEST_CASE("GDB: storage_get_status returns 0 for inactive channel") {
        StorageFixture s;
        s.write_reg(N8_DISK_CHAN, 0x00);
        CHECK(storage_get_status() == 0x00);
    }
}
