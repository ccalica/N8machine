// Link-time stubs for gui_console and ImGui.
// These satisfy unresolved symbols from emulator_show_* and emu_dis6502_window.
// Never called in tests.
//
// ImGui signatures derived from:
//   nm -u build/emulator.o build/emu_dis6502.o | grep imgui | c++filt

#include <string>
#include <deque>

// ---- gui_console stubs ----
static std::deque<std::string> stub_console_buffer;

void gui_con_printmsg(char* msg) {
    stub_console_buffer.push_back(std::string(msg));
}
void gui_con_printmsg(std::string msg) {
    stub_console_buffer.push_back(msg);
}
void gui_con_init() {}
void gui_show_console_window(bool&) {}

std::deque<std::string>& stub_get_console_buffer() {
    return stub_console_buffer;
}
void stub_clear_console_buffer() {
    stub_console_buffer.clear();
}

// ---- ImGui linker stubs ----
// Exact signatures from nm -u output (demangled):

struct ImVec2 {
    float x, y;
    ImVec2() : x(0), y(0) {}
    ImVec2(float a, float b) : x(a), y(b) {}
};
struct ImVec4 {
    float x, y, z, w;
    ImVec4() : x(0), y(0), z(0), w(0) {}
    ImVec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

struct ImGuiInputTextCallbackData;

namespace ImGui {
    bool Begin(const char*, bool*, int) { return true; }
    void End() {}
    void Text(const char*, ...) {}
    void TextColored(const ImVec4&, const char*, ...) {}
    void SameLine(float, float) {}
    bool Checkbox(const char*, bool*) { return false; }
    bool InputText(const char*, char*, unsigned long, int,
                   int (*)(ImGuiInputTextCallbackData*), void*) { return false; }
    void BeginChild(const char*, const ImVec2&, bool, int) {}
    void EndChild() {}
    void SetScrollY(float) {}
    float GetScrollMaxY() { return 0.0f; }
}
