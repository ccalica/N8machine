# Test Harness Spec v1 -- Implementer Critique (Critic A)

**Perspective**: Practical implementer. Will this spec produce a working `make test` on the first attempt?

**Source ground truth**: All findings verified against actual source files in `src/`.

---

## 1. Build Feasibility

### Issue 1: CHIPS_IMPL Duplicate Symbol Catastrophe

**Severity**: CRITICAL

`emulator.cpp` line 1 has `#define CHIPS_IMPL` before `#include "m6502.h"`. This causes the entire m6502 implementation (3000+ lines of function bodies) to be compiled into `emulator.o`. The m6502 API functions (`m6502_init`, `m6502_tick`, `m6502_a`, `m6502_x`, etc.) all have their definitions inside the `#ifdef CHIPS_IMPL` block.

The `CpuFixture` (Section 6.1) calls `m6502_init(&cpu, &desc)` and `m6502_tick(&cpu, pins)`. These symbols are already defined in `emulator.o` (which is linked into the test binary). The fixture code lives in a test `.cpp` file that includes `m6502.h`. If any test `.cpp` file also defines `CHIPS_IMPL`, you get duplicate symbols. If no test file defines `CHIPS_IMPL`, the function declarations are available from the header (they're declared outside the `#ifdef` block), and implementations come from `emulator.o`. This actually works -- but only if no test file defines `CHIPS_IMPL`.

**The spec never mentions this constraint.** An implementer unfamiliar with the chips library pattern could easily add `#define CHIPS_IMPL` in `test_helpers.h` or `test_cpu.cpp` thinking they need the implementation, which would cause every m6502 function to be multiply defined.

**Recommendation**: Add an explicit warning: "NEVER define `CHIPS_IMPL` in any test file. The m6502 implementation is already compiled into `emulator.o`. Test files include `m6502.h` for declarations only."

### Issue 2: `ImGuiInputTextFlags` Enum/Constant Not Stubbed

**Severity**: CRITICAL

`emulator.cpp` line 279 uses `ImGuiInputTextFlags_AllowTabInput` as a static variable initializer:
```cpp
static ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
```

This is NOT a function call -- it's an enum constant from `imgui.h`. It is compiled into `emulator.o` at build time with the real ImGui headers. Since it's a compile-time constant already baked into the `.o`, this is fine for linking.

However, the spec's ImGui stubs define function signatures in the `ImGui::` namespace but do not provide `ImGuiInputTextFlags` as a type or any of its enum values. Since `emulator.o` is the pre-compiled production object, these enum values are already resolved. **This specific case is not a blocker**, but the spec's statement "compile, check for unresolved symbols, add stubs as needed" is the real safety net here. The spec should acknowledge that the stubs only need to cover *linker* symbols (functions), not compile-time constants.

**Recommendation**: Clarify in Section 5.2 that only function symbols need stubs. Enum/constant values are resolved at compile time in the production objects.

### Issue 3: `InputText` Stub Signature Mismatch

**Severity**: IMPORTANT

The spec's `ImGui::InputText` stub signature is:
```cpp
bool InputText(const char*, char*, unsigned long, int, void*, void*) { return false; }
```

The real ImGui `InputText` signature is:
```cpp
bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
```

`ImGuiInputTextCallback` is a function pointer type (`int (*)(ImGuiInputTextCallbackData*)`), not `void*`. On most platforms this won't matter (pointer types have the same size), but on strict compilers or with name mangling, `void*` vs a function pointer type will produce a different mangled symbol. The linker will fail to resolve the symbol.

Additionally, `size_t` is `unsigned long` on 64-bit Linux but `unsigned long long` on some platforms. The mangled name depends on the exact type. Since the production `.o` files are compiled with the real ImGui headers, the mangled symbol will use ImGui's actual types.

**Recommendation**: The spec should not try to guess ImGui signatures. Instead, provide a procedure: (1) build production objects, (2) run `nm -u build/emulator.o build/emu_dis6502.o | grep -i imgui | c++filt`, (3) generate stubs matching the exact demangled signatures. Alternatively, include the real `imgui.h` in `test_stubs.cpp` for type definitions only.

### Issue 4: `ImVec2` and `ImVec4` Struct Redefinitions

**Severity**: IMPORTANT

The spec defines `ImVec2` and `ImVec4` structs in `test_stubs.cpp`. These are needed because functions like `ImGui::BeginChild` take `ImVec2` by value. But the spec's stub definitions must match the real ImGui struct layout exactly for the linker to accept them (C++ name mangling includes parameter types).

Real ImGui `ImVec2` has `ImVec2()` and `ImVec2(float _x, float _y)` constructors. The spec's version matches. But if the real ImGui version has additional members (like `operator[]` in newer versions), the mangled names might differ.

**Recommendation**: Same as Issue 3 -- derive stubs from actual `nm` output rather than guessing.

### Issue 5: Missing `SameLine` Overloads

**Severity**: IMPORTANT

The spec stubs `SameLine(float, float)` but ImGui's actual signature is `void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f)`. The production code calls `ImGui::SameLine()` with 0 args (line 285), 1 arg (line 334), and 2 would be via defaults. With C++ name mangling, default parameters don't change the symbol -- `SameLine(float, float)` is the only symbol generated regardless of how many args the caller provides. This should be fine.

However, the production code also calls `ImGui::SameLine(80)` (main.cpp line 223) with an int literal. Since `main.o` is NOT linked into the test binary, this is not an issue. **No action needed** -- but this illustrates why exhaustive `nm` analysis is the correct approach.

**Recommendation**: Nitpick only. No change needed.

### Issue 6: `make test` Dependency Chain

**Severity**: IMPORTANT

The spec says `make && make test` as two separate commands. The Makefile `test` target depends on `$(TEST_SRC_OBJS)` which are `$(BUILD_DIR)/emulator.o` etc. But there is no dependency declared between `test` and the default `all` target. If someone runs `make test` without `make` first and the production `.o` files don't exist, Make will try to build them using the `$(BUILD_DIR)/%.o:$(SRC_DIR)/%.cpp` pattern rule. This rule uses `$(CXXFLAGS)` which includes `-I$(IMGUI_DIR)` and SDL2 flags -- so it should work if SDL2-dev is installed.

But the spec says "test sources compiled with TEST_CXXFLAGS which has no SDL/ImGui flags." This is only true for the test `.cpp` files. The production `.o` files will be rebuilt with production CXXFLAGS if they're missing. This is actually correct behavior but the spec's "Build Sequence" section is misleading.

**Recommendation**: Either add `test: all $(TEST_EXE)` to make the dependency explicit, or document that `make test` will build production objects if needed (requiring SDL2-dev and ImGui headers even for the test build).

### Issue 7: `emu_labels.o` Links Against File I/O That Calls `exit(-1)`

**Severity**: IMPORTANT

`emu_labels.o` is linked into the test binary. `emu_labels_init()` calls `emu_labels_load()` which does `fopen("N8firmware.sym")` and if the file doesn't exist, calls `exit(-1)`.

The spec says tests bypass `emulator_init()` entirely. True -- the `EmulatorFixture` doesn't call `emulator_init()`. But `emu_labels_init()` is still a callable function in the binary. If any test accidentally calls it, or if a future test needs it, the test process crashes immediately because `N8firmware.sym` is not present.

More critically: `emu_labels.o` is linked. The `labels[65536]` global array (an array of 65536 `std::list<std::string>`) will be initialized at static construction time. That's fine -- it's just empty lists. But `emu_labels_init()` existing in the binary is a latent risk.

The label tests (T90-T94) call `emu_labels_add()` and `emu_labels_clear()` directly, which is safe. But the spec should explicitly warn: "Never call `emu_labels_init()` or `emu_labels_load()` from tests -- they require `N8firmware.sym` on disk."

**Recommendation**: Add warning to Section 5 about `emu_labels_init()` / `emu_labels_load()` file dependency. Consider adding a note in the label test section.

---

## 2. Stub Completeness

### Issue 8: Exhaustive ImGui Symbol Audit Missing

**Severity**: IMPORTANT

The spec lists ~15 ImGui functions to stub. Here is what the production `.o` files actually reference based on source analysis:

**From `emulator.o` (`emulator_show_memdump_window`, `emulator_show_status_window`):**
- `ImGui::Begin(const char*, bool*, int)` -- stubbed
- `ImGui::End()` -- stubbed
- `ImGui::Text(const char*, ...)` -- stubbed
- `ImGui::SameLine(float, float)` -- stubbed
- `ImGui::Checkbox(const char*, bool*)` -- stubbed
- `ImGui::InputText(...)` -- stubbed (but signature may be wrong, see Issue 3)
- `ImGui::BeginChild(const char*, const ImVec2&, bool, int)` -- stubbed
- `ImGui::EndChild()` -- stubbed
- `ImVec2` constructors -- stubbed
- `ImGuiInputTextFlags_AllowTabInput` -- compile-time constant, no stub needed

**From `emu_dis6502.o` (`emu_dis6502_window`):**
- All of the above, plus:
- `ImGui::TextColored(const ImVec4&, const char*, ...)` -- stubbed
- `ImGui::SetScrollY(float)` -- stubbed
- `ImGui::GetScrollMaxY()` -- stubbed
- `ImVec4` construction -- stubbed (but note: real ImVec4 has a constructor `ImVec4(float,float,float,float)` which the spec's struct lacks)

**Missing from spec stubs:**
- `ImVec4` constructor `ImVec4(float, float, float, float)`: The code at `emu_dis6502.cpp:219` constructs `ImVec4(0.0f,1.0f,0.0f,1.0f)`. The spec's `ImVec4` struct has no constructor at all -- it's an aggregate `{ float x, y, z, w; }`. If the real ImGui `ImVec4` has an explicit constructor, the mangled symbol from the production `.o` won't match. This needs a proper constructor in the stub.

**Recommendation**: Add constructor to `ImVec4`: `ImVec4() : x(0), y(0), z(0), w(0) {} ImVec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}`

### Issue 9: `gui_console.o` Stubs Are Incomplete

**Severity**: MINOR

The `gui_console.o` is excluded from the test link, and the spec provides stubs for its exported symbols. Let me verify:

- `gui_con_printmsg(char*)` -- stubbed
- `gui_con_printmsg(std::string)` -- stubbed
- `gui_con_init()` -- stubbed
- `gui_show_console_window(bool&)` -- stubbed

These match `gui_console.h` exactly. Complete.

### Issue 10: `emu_dis6502.o` References `gui_con_printmsg` -- Covered

**Severity**: NO ISSUE (verification note)

`emu_dis6502.cpp` calls `gui_con_printmsg()` at lines 137 and 142. Since `gui_console.o` is excluded and stubs provide these symbols, this is correctly handled.

### Issue 11: `emu_labels.o` References `gui_con_printmsg` -- Covered

**Severity**: NO ISSUE (verification note)

`emu_labels.cpp` line 39 calls `gui_con_printmsg()`. Covered by stubs.

### Issue 12: `emu_tty.o` References `emu_set_irq` and `emu_clr_irq` -- Covered

**Severity**: NO ISSUE (verification note)

These are defined in `emulator.cpp` and exported via `emulator.h`. Since `emulator.o` is linked, these symbols resolve.

---

## 3. Fixture Correctness

### Issue 13: `tty_decode()` Takes `uint64_t&` (Reference), Not Value

**Severity**: CRITICAL

The `tty_decode()` function signature is:
```cpp
void tty_decode(uint64_t &pins, uint8_t dev_reg);
```

It takes pins **by reference** and modifies them (calls `M6502_SET_DATA(pins, data_bus)`).

The spec's `make_read_pins()` and `make_write_pins()` helpers return `uint64_t` by value:
```cpp
inline uint64_t make_read_pins(uint16_t addr) {
    uint64_t p = 0;
    M6502_SET_ADDR(p, addr);
    p |= M6502_RW;
    return p;
}
```

A test like:
```cpp
tty_decode(make_read_pins(0xC100), 0);  // WON'T COMPILE
```
will NOT compile because you can't bind a non-const lvalue reference to an rvalue.

The test code must store the result in a local variable first:
```cpp
uint64_t p = make_read_pins(0xC100);
tty_decode(p, 0);
uint8_t result = M6502_GET_DATA(p);
```

The spec doesn't show actual test code using these helpers, so the implementer will discover this on the first compile. Not catastrophic, but it's a design flaw in the helper API.

**Recommendation**: Document that `make_read_pins` / `make_write_pins` return values must be stored in a local variable before passing to `tty_decode()`. Or redesign the helpers to take a `uint64_t&` output parameter.

### Issue 14: `tty_decode()` Called With Register Offset, Not Address

**Severity**: IMPORTANT

Looking at `emulator_step()` line 142-144:
```cpp
BUS_DECODE(addr, 0xC100, 0xFFF0) {
    const uint8_t dev_reg = (addr - 0xC100) & 0x00FF;
    tty_decode(pins, dev_reg);
}
```

`tty_decode` receives a register number (0-3), NOT the full address. The spec's test cases T71-T78 show:
```
T71: tty_decode(read_pins, 0)
T72: tty_decode(read_pins, 1)
```

This is correct -- the dev_reg is already extracted. But the pin mask passed to `tty_decode` doesn't need to have the address set to `0xC100`, because `tty_decode` doesn't look at the address -- it only checks `M6502_RW` and calls `M6502_SET_DATA`/`M6502_GET_DATA`. The `make_read_pins(addr)` / `make_write_pins(addr, data)` helpers set an address, but that address is never used by `tty_decode`.

**Recommendation**: Clarify that the address in pin helpers for TTY tests is irrelevant (only the RW bit and data matter). Simplify helpers or add a comment.

### Issue 15: `EmulatorFixture` Calls `m6502_init()` With Uninitialized `desc`

**Severity**: MINOR

The fixture does:
```cpp
memset(mem, 0, sizeof(uint8_t) * 65536);
...
pins = m6502_init(&cpu, &desc);
```

But `desc` is the global `m6502_desc_t desc;` from `emulator.cpp`. It's zero-initialized as a global, and `memset(&desc, 0, sizeof(desc))` is never called in the fixture (it's only in `CpuFixture`). The EmulatorFixture does NOT zero `desc` -- it relies on it being a zero-initialized global. This is actually fine for the first test, but if any test modifies `desc`, subsequent tests will inherit the modification.

Wait -- re-reading the spec, the `EmulatorFixture` constructor does NOT `memset(&desc, 0, ...)`. It only clears `mem`, `frame_buffer`, `bp_mask`, resets `tick_count`, and calls `m6502_init(&cpu, &desc)`. This is actually OK because `desc` is a global zero-initialized once, and no test code modifies it. But it's fragile.

**Recommendation**: Add `memset(&desc, 0, sizeof(desc));` to `EmulatorFixture` constructor for defensive reset.

### Issue 16: `emulator_step()` Calls `tty_tick()` Which Calls `tty_kbhit()` and Potentially `getch()`

**Severity**: IMPORTANT

Every call to `emulator_step()` (used by `EmulatorFixture::step_n()`) calls `tty_tick(pins)`. `tty_tick` calls `tty_kbhit()` which does a `select()` on stdin with zero timeout. In a non-interactive test environment (CI/CD), this is safe (returns 0 immediately). But if stdin has unexpected data (piped input, test runner quirks), `tty_kbhit()` returns 1, then `getch()` is called which does `read(0, ...)`. If `read()` fails, `exit(-1)` is called, killing the test process.

The spec mentions this concern in Section 5.3 but dismisses it: "`tty_kbhit()` returns 0 immediately when no keyboard input is pending." This is true for interactive terminals and pipe-closed stdin. But not guaranteed in all CI environments.

**Recommendation**: Add a note that running tests with stdin closed or redirected from `/dev/null` is safest: `make test < /dev/null`. Or mention this as a known risk.

### Issue 17: `emulator_step()` Calls `IRQ_CLR()` Which Zeroes `mem[0x00FF]` Every Tick

**Severity**: IMPORTANT

The spec correctly notes this in the T69/T70 source verification note. But the implications ripple further than acknowledged:

- Any test that calls `emulator_step()` will have `mem[0x00FF]` zeroed at the start of each tick.
- `emu_set_irq(1)` sets `mem[0x00FF] |= 0x02`. But the next `emulator_step()` call immediately clears it.
- Tests T69/T70 say to call `emu_set_irq`/`emu_clr_irq` "directly without `emulator_step()` in between." This works for the IRQ register tests.
- But T76 ("Read In Data clears IRQ") requires: push byte to buffer, set IRQ bit 1, read reg 3 to drain. This involves `tty_decode()` which clears the IRQ when the buffer empties. If the test calls `tty_decode` directly (not via `emulator_step`), `IRQ_CLR()` is not called, so the test can observe the IRQ state change. This is correct.
- T101 ("IRQ triggers vector") requires IRQ to persist long enough for the CPU to respond. Since `emulator_step()` clears and re-sets IRQ each tick (via `tty_tick` re-asserting if buffer non-empty), the IRQ line behavior is: clear -> tty_tick sets if data -> IRQ pin driven. This should work because `tty_tick` re-asserts IRQ each tick if `tty_buff.size() > 0`. But the test must first inject data into `tty_buff` -- which is file-static and inaccessible.

**Recommendation**: This interaction is subtle and error-prone. Add detailed implementation notes for T101.

---

## 4. Test Case Executability

### Issue 18: T74, T75, T76 -- Impossible Without Production Code Change

**Severity**: CRITICAL

Tests T74 ("Read In Status with data"), T75 ("Read In Data"), and T76 ("Read In Data clears IRQ") all require bytes to be present in `tty_buff`. The spec states:

> "T74, T75, T76 require pushing bytes into `tty_buff`. Since `tty_buff` is file-static, this needs either: (a) a `tty_inject_char()` accessor added to `emu_tty.cpp`, or (b) testing input flow through the full emulator path."

Option (b) means calling `tty_tick()` which calls `tty_kbhit()` / `getch()` -- reading from actual stdin. This is not feasible in automated tests.

The spec says "option (a) is a minimal 2-line addition to production code and is recommended as a follow-up." But these are P0 tests. You cannot implement P0 tests as a "follow-up." Either:
1. Accept a production code change (add `tty_inject_char()`)
2. Demote T74-T76 to P2/DEFERRED
3. Violate the "zero production changes" principle upfront

The same issue affects T79 ("tty_reset clears buffer") -- you need to push bytes first to verify they're cleared.

**Recommendation**: Either add `tty_inject_char(uint8_t c)` and `tty_buff_size()` to `emu_tty.cpp` as a planned exception to the "zero changes" rule, or demote these tests. Three P0 tests that cannot be implemented defeat the purpose.

### Issue 19: T67 -- Frame Buffer/TTY Boundary Test Logic Is Wrong

**Severity**: IMPORTANT

T67 says: "LDA #$99; STA $C100 -- frame_buffer untouched; hits TTY region."

Let's trace the bus decode. Address 0xC100:
- `BUS_DECODE(addr, 0xC000, 0xFF00)`: `0xC100 & 0xFF00 = 0xC100 != 0xC000`. Does NOT match frame buffer. Correct.
- `BUS_DECODE(addr, 0xC100, 0xFFF0)`: `0xC100 & 0xFFF0 = 0xC100 == 0xC100`. Matches TTY. Correct.

But the test says "frame_buffer untouched." How do you verify "untouched"? The frame buffer is `memset` to 0 in the fixture. After `STA $C100`, `mem[0xC100]` gets written (line 117 -- RAM write happens first), but `frame_buffer` is NOT written because the address doesn't match the frame buffer decode range. The test should check `frame_buffer[0]` through `frame_buffer[255]` are all still 0.

Also: this write goes to the TTY device (register 0 = Out Status). `tty_decode` handles a write to register 0 with `;;;` (no-op). So the test is actually: "write to 0xC100 does not touch frame_buffer AND hits TTY silently." This is fine but the test description is misleading -- it says "hits TTY region" but doesn't verify TTY behavior.

**Recommendation**: Clarify what "frame_buffer untouched" means in the assertion. Suggest checking `frame_buffer[0] == 0` (or all 256 bytes). Clarify that the TTY side effect is a no-op write to register 0.

### Issue 20: T23 -- Reset Vector Test Needs Boot Sequence

**Severity**: MINOR

T23: "mem[0xFFFC]=0x00, mem[0xFFFD]=0xD0. Boot." Expected: `pc() == 0xD000`.

Using `CpuFixture`: after `m6502_init()`, the CPU starts the reset sequence. `boot()` calls `tick()` in a loop until `M6502_SYNC` is set. The reset sequence is 7 ticks. After boot, `pc()` should indeed be 0xD000.

This works. But the test description says "Boot" without specifying how many ticks or which helper. The `CpuFixture::boot()` method ticks until SYNC. This is correct because SYNC indicates the start of the first real instruction fetch, meaning PC has been loaded from the reset vector.

**Recommendation**: No change needed. The fixture API is clear enough.

### Issue 21: T25-T27 -- LDA Immediate Tests Need Precise Tick Counting

**Severity**: MINOR

T25: "A9 42 (LDA #$42)". Using CpuFixture, load at 0x0400, set reset vector to 0x0400, boot, then need to execute one instruction. After `boot()`, the CPU is at the start of the instruction at 0x0400 (SYNC is set). Then `run_instructions(1)` calls `run_until_sync()` which ticks until the NEXT SYNC. After this, the LDA has executed and the CPU is at the opcode fetch of the NEXT instruction.

The question is: after `run_instructions(1)`, has LDA #$42 completed? `run_until_sync()` ticks until SYNC fires again, which means the next instruction's opcode has been fetched. At that point, the previous instruction (LDA) has fully completed, and `a()` returns 0x42. This is correct.

But what about the instruction AFTER the LDA? `mem[0x0402]` is 0x00 (BRK). This will trigger a BRK sequence. If tests only check `a()` after `run_instructions(1)`, the BRK hasn't started executing yet (only its opcode was fetched). So this is fine.

**Recommendation**: Add a trailing `EA` (NOP) or two after each test program to prevent BRK from firing during the sync-detection loop. This is defensive -- not strictly required for checking `a()` after `run_instructions(1)`, but prevents confusion if tests evolve.

### Issue 22: T39 -- JSR/RTS Test Description Is Ambiguous

**Severity**: MINOR

T39: "JSR $0410 at 0x0400. RTS at 0x0410. NOP at 0x0403." Expected: "PC past 0x0403 after 3 instrs."

The byte sequence would be:
- 0x0400: `20 10 04` (JSR $0410)
- 0x0403: `EA` (NOP)
- 0x0410: `60` (RTS)

After JSR, PC = 0x0410. After RTS, PC = 0x0403 (return address + 1). After NOP, PC = 0x0404.

So after 3 instructions (JSR, RTS, NOP), `pc()` should be 0x0404. The expected result "PC past 0x0403" is correct but vague. An implementer needs to know the exact expected value.

**Recommendation**: Change expected to `pc() == 0x0404`.

### Issue 23: T42 -- BEQ Not Taken Test Description Incomplete

**Severity**: MINOR

T42: "A9 01 F0 02 A9 55 ..." Expected: `a()==0x55`.

After `LDA #$01`, Z=0. `BEQ +2` is not taken. Fall through to `A9 55` (LDA #$55). So `a()==0x55`. But the "..." implies more bytes. The test program should be: `A9 01 F0 02 A9 55 EA` (NOP at end). The `a()==0x55` check requires running 3 instructions (LDA #1, BEQ, LDA #$55). This works.

**Recommendation**: Complete the byte sequences. Remove "..." -- specify exact bytes including trailing NOPs.

### Issue 24: T69/T70 -- IRQ Register Tests Can't Use `emulator_step()`

**Severity**: IMPORTANT

As the spec notes, `emulator_step()` calls `IRQ_CLR()` at the top of every tick. T69 says: "`emu_set_irq(1)` -- Expected: `mem[0x00FF] & 0x02`."

If the test uses `EmulatorFixture` and calls `emu_set_irq(1)` then checks `mem[0x00FF]`, this works because `emu_set_irq` directly writes to `mem[0x00FF]` and no `emulator_step()` is called between set and check. But the test is in the "bus" suite which uses `EmulatorFixture`. The fixture constructor calls `m6502_init` but does NOT call `emulator_step()`, so `IRQ_CLR()` hasn't run. Fine.

But this means T69/T70 are not really "bus decode" tests -- they're testing `emu_set_irq`/`emu_clr_irq` utility functions. They belong in `test_utils.cpp` or their own suite.

**Recommendation**: Move T69/T70 to `test_utils.cpp` or rename the test to clarify it doesn't involve bus decode.

### Issue 25: T80-T89 -- Disassembler Tests Use Global `mem[]`

**Severity**: MINOR

The spec correctly notes this. `emu_dis6502_decode()` reads from the global `mem[]` array. Tests must write to `mem[]` at the target address, then call `emu_dis6502_decode()`. Since `mem[]` is extern-declared in `emulator.h`, this is accessible.

But what about cleanup between tests? If test T80 writes `mem[0x0400]=0xEA` and test T81 writes `mem[0x0400]=0xA9, mem[0x0401]=0x42`, there's no conflict (T81 overwrites T80's data). But if tests use different addresses, leftover data could theoretically confuse results.

**Recommendation**: Each disassembler test should `memset(mem, 0, 65536)` at the top, or use a `SUBCASE` / `TEST_CASE` that resets memory. The spec doesn't provide a fixture for disassembler tests.

### Issue 26: T90-T94 -- `emu_labels_add` and `emu_labels_clear` Not Declared in Header

**Severity**: IMPORTANT

The spec correctly notes: "`emu_labels_add()` and `emu_labels_clear()` are defined in `emu_labels.cpp` but NOT declared in `emu_labels.h`."

Verified: `emu_labels.h` declares `emu_labels_get`, `emu_labels_console_list`, `emu_labels_load`, `emu_labels_init`. It does NOT declare `emu_labels_add` or `emu_labels_clear`.

The spec says "Test file needs `extern` declarations for these functions." This is correct. The test file must add:
```cpp
extern void emu_labels_add(uint16_t addr, char* label);
extern void emu_labels_clear();
```

But note the signature: `emu_labels_add(uint16_t addr, char * label)` -- the second parameter is `char*`, not `const char*`. This means test code like `emu_labels_add(0xD000, "main")` will trigger a `-Wwrite-strings` warning (or error with `-Werror`) because string literals are `const char*` in C++. The production code compiles because it's C++11 with `-Wall` but no `-Werror`.

The test build uses `TEST_CXXFLAGS = -std=c++11 -g -Wall -Wformat` -- no `-Werror`. So it will compile with a warning. But it's ugly.

**Recommendation**: Note the `char*` vs `const char*` issue. Either cast in test code (`(char*)"main"`) or mention that a production code fix to `const char*` would be cleaner.

### Issue 27: T101 -- IRQ Integration Test Requires `tty_buff` Injection

**Severity**: IMPORTANT

T101: "Set IRQ vector, enable IRQ, CLI in program -- CPU reaches IRQ handler."

For the CPU to take an IRQ, the `M6502_IRQ` pin must be asserted. In `emulator_step()`, this happens when `mem[0x00FF] != 0` (line 97-107). `mem[0x00FF]` is set by `emu_set_irq()` which is called by `tty_tick()` when `tty_buff.size() > 0`.

So to trigger an IRQ through `emulator_step()`, there must be data in `tty_buff`. But `tty_buff` is file-static and inaccessible (same as Issue 18). The alternative is to call `emu_set_irq(1)` between steps, but `emulator_step()` calls `IRQ_CLR()` at the top, then `tty_tick()`. So manually calling `emu_set_irq()` between steps won't work -- it gets cleared on the next step.

The only way T101 works is if `tty_buff` has data, so `tty_tick()` re-asserts the IRQ each tick. But we can't inject data into `tty_buff`.

**Recommendation**: T101 is blocked by the same issue as T74-T76. It requires either `tty_inject_char()` or a different approach to IRQ assertion. Consider adding `emu_force_irq()` to `emulator.cpp` as a test hook, or accept the minimal production change.

---

## 5. Missing Pieces

### Issue 28: No `test_helpers.h` Template Provided

**Severity**: IMPORTANT

The spec references `test_helpers.h` throughout as the location for:
- `extern m6502_t cpu;`
- `extern m6502_desc_t desc;`
- `extern uint64_t pins;`
- `extern uint8_t frame_buffer[];`
- `extern uint64_t tick_count;`
- `CpuFixture` struct definition
- `EmulatorFixture` struct definition
- `make_read_pins()` / `make_write_pins()` helpers
- `stub_get_console_buffer()` / `stub_clear_console_buffer()` declarations

But the spec never provides a complete, copy-paste-ready `test_helpers.h`. The implementer must assemble it from Sections 5.5, 6.1, 6.2, 6.3, 6.4, and 6.5 -- scattered across 4 pages.

**Recommendation**: Add a complete `test_helpers.h` listing in an appendix or consolidate in Section 6.

### Issue 29: No `test_main.cpp` Content Shown

**Severity**: MINOR

Section 3 says `test_main.cpp` contains `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. The complete file would be:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

Two lines, but an implementer might wonder if anything else is needed.

**Recommendation**: Show the complete 2-line file.

### Issue 30: doctest.h Acquisition Not Specified

**Severity**: MINOR

The spec says "Vendor `doctest.h` directly in the repository" but doesn't say which version or where to download it.

**Recommendation**: Specify the doctest version (e.g., 2.4.11) and URL: `https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h`.

### Issue 31: Disassembler Output Format Not Specified for Assertions

**Severity**: IMPORTANT

T81 expects the output to contain "LDA #$42". But what exactly does `emu_dis6502_decode()` return? It writes to a `char*` buffer and returns the instruction length.

Looking at the source, the format string is:
```cpp
snprintf(menomic, m_len,"%s %s%s%s", opcode, pre, address, post);
```

For LDA #$42 (opcode 0xA9):
- `opcode_props[0xA9]` = `{2, 29, 1}` (length 2, instruction index 29=LDA, mode 1=immediate)
- `instruction[29]` = "LDA"
- `modes[1]` = `{"#", ""}`
- `address` = "$42" (from `snprintf(address, 8, "$%02X", mem[addr+1])`)
- Result: `"LDA #$42"`

So `CHECK(std::string(buf).find("LDA #$42") != std::string::npos)` works. But the spec says "contains 'LDA #$42'" -- it doesn't specify whether to use `CHECK`, `REQUIRE`, or how to do the string match. An implementer must decide the assertion pattern.

**Recommendation**: Provide a helper macro or example assertion pattern: `CHECK(std::string(buf).find("LDA #$42") != std::string::npos)`. Or use doctest's `CHECK(strstr(buf, "LDA #$42") != nullptr)`.

### Issue 32: `emu_dis6502_decode()` Signature Uses `int addr`, Not `uint16_t`

**Severity**: MINOR

The function is `int emu_dis6502_decode(int addr, char *menomic, int m_len)`. The spec doesn't mention this -- tests using `uint16_t` addresses will implicitly convert. Not a problem, but good to know.

### Issue 33: No Guidance on Test Execution Order or Parallelism

**Severity**: MINOR

doctest runs tests in registration order by default. The `EmulatorFixture` tests share global state. If tests run in parallel (doctest doesn't do this by default), global state corruption would occur. The spec should state that tests run sequentially.

**Recommendation**: Add a note that tests must run sequentially (doctest default). No parallel test execution.

---

## 6. Overcomplications

### Issue 34: Pin Construction Helpers Overengineered for TTY Tests

**Severity**: MINOR

`make_read_pins(uint16_t addr)` sets an address that `tty_decode` never reads. A simpler approach:
```cpp
inline uint64_t make_read_pins() {
    return M6502_RW;  // just set the read bit
}
inline uint64_t make_write_pins(uint8_t data) {
    uint64_t p = 0;
    M6502_SET_DATA(p, data);
    return p;
}
```

The address parameter is unnecessary for `tty_decode` (which receives the register offset separately). However, keeping the address makes the helpers reusable for bus-level tests where address matters. Trade-off.

**Recommendation**: Keep as-is but add a comment explaining why address is included.

### Issue 35: Console Buffer Inspection May Not Be Needed for P0

**Severity**: NITPICK

The `stub_get_console_buffer()` / `stub_clear_console_buffer()` infrastructure is well-designed but only used for breakpoint-hit message verification (T97 could optionally check it). No P0 test strictly requires inspecting console output. It's good infrastructure for P1+ but could be deferred.

**Recommendation**: Keep it -- it's 5 lines of code and useful soon enough.

### Issue 36: 81 Test Cases May Be Overscoped for v1

**Severity**: NITPICK

The spec defines 81 test cases across 7 modules with 3 priority tiers. For an initial harness, implementing all P0 tests (~42) is a substantial effort. The spec could benefit from identifying a "smoke test" subset of ~10 tests that validate the harness itself works (build, link, fixture setup, one test per module).

**Recommendation**: Add a "Phase 0" milestone: ~10 tests covering one from each module, proving the build pipeline and fixtures work end-to-end. Then implement remaining P0 tests.

---

## Summary

| Severity | Count | Key Issues |
|----------|-------|------------|
| CRITICAL | 3 | CHIPS_IMPL duplicate symbol risk (#1), tty_decode reference parameter vs. rvalue (#13), tty_buff inaccessible for P0 tests (#18) |
| IMPORTANT | 12 | ImGui stub signatures (#3, #4, #8), build deps (#6, #7), emulator_step IRQ behavior (#17, #24, #27), missing declarations (#26), missing consolidated helpers file (#28), disasm assertion pattern (#31) |
| MINOR | 9 | Various documentation gaps, test description ambiguities, defensive coding suggestions |
| NITPICK | 2 | Console buffer timing, scope |

### Top 3 Actions Before Implementation

1. **Resolve the `tty_buff` access problem** (Issues #18, #27). Either accept `tty_inject_char()` as a production change or demote T74-T76, T79, T101 from P0. This blocks 5 test cases including critical TTY and IRQ coverage.

2. **Add CHIPS_IMPL warning** (Issue #1) and **derive ImGui stubs from `nm` output** (Issues #3, #4, #8). The current stub signatures are educated guesses. Five minutes with `nm -u` and `c++filt` will produce correct stubs.

3. **Provide a complete `test_helpers.h`** (Issue #28). The implementer should not have to assemble it from 5 different spec sections.
