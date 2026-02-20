// Dear ImGui: standalone example application for SDL2 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs


#include <bits/stdc++.h>
#include <stdlib.h>
using namespace std;

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_timer.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

#include <stdio.h>
#include <chrono>
#include <thread>

#include "emulator.h"
#include "emu_dis6502.h"
#include "machine.h"
#include "utils.h"
#include "gdb_stub.h"
#include "m6502.h"
#include "emu_tty.h"

const char* glsl_version;
SDL_WindowFlags window_flags;
SDL_Window* window;
SDL_GLContext gl_context;

// ---- Emulator state (file scope for GDB callback access) ----
static bool run_emulator = false;
static bool step_emulator = false;
static bool gdb_halted = false;
static bool bp_enable = false;

// Externs from emulator.cpp needed by GDB callbacks
extern m6502_t cpu;
extern uint64_t pins;

// ---- GDB stub callbacks ----

static uint8_t gdb_read_reg8(int reg_id) {
    switch (reg_id) {
        case 0: return emulator_read_a();
        case 1: return emulator_read_x();
        case 2: return emulator_read_y();
        case 3: return emulator_read_s();
        case 4: return emulator_read_p();
        default: return 0;
    }
}

static uint16_t gdb_read_reg16(int reg_id) {
    if (reg_id == 5) return emulator_getpc();
    return 0;
}

static void gdb_write_reg8(int reg_id, uint8_t val) {
    switch (reg_id) {
        case 0: emulator_write_a(val); break;
        case 1: emulator_write_x(val); break;
        case 2: emulator_write_y(val); break;
        case 3: emulator_write_s(val); break;
        case 4: emulator_write_p(val); break;
    }
}

static void gdb_write_reg16(int reg_id, uint16_t val) {
    if (reg_id == 5) emulator_write_pc(val);
}

static uint8_t gdb_read_mem(uint16_t addr) {
    return mem[addr];
}

static void gdb_write_mem(uint16_t addr, uint8_t val) {
    mem[addr] = val;
}

static int gdb_step_instruction(void) {
    // Step until SYNC (next instruction boundary), guard=16 ticks
    int ticks = 0;
    do {
        emulator_step();
        ticks++;
        if (ticks >= 16) return 4; // SIGILL — likely jammed
    } while (!(pins & M6502_SYNC));
    return 5; // SIGTRAP
}

static void gdb_set_breakpoint(uint16_t addr) {
    bp_mask[addr] = true;
    bp_enable = true;
    emulator_enablebp(true);
}

static void gdb_clear_breakpoint(uint16_t addr) {
    bp_mask[addr] = false;
    // Disable BP scanning if no breakpoints remain
    bool any = false;
    for (int i = 0; i < 65536 && !any; i++) any = bp_mask[i];
    if (!any) { bp_enable = false; emulator_enablebp(false); }
}

static void gdb_continue_exec(void) {
    run_emulator = true;
}

static void gdb_halt(void) {
    run_emulator = false;
}

static uint16_t gdb_get_pc(void) {
    return emulator_getpc();
}

static int gdb_get_stop_reason(void) {
    return 5; // SIGTRAP default
}

static void gdb_reset(void) {
    // D47: Use M6502_RES pin, NOT emulator_reset()
    pins |= M6502_RES;
    tty_reset();
}


int SDL_GL_Init() {
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    window = SDL_CreateWindow("N8Machine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    gl_context = SDL_GL_CreateContext(window);

    SDL_GL_MakeCurrent(window, gl_context);
    //SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_GL_SetSwapInterval(0); // disable vsync

    return 0;
}
// Main code
int main(int, char**)
{
    int rtn;
    if((rtn = SDL_GL_Init()) != 0) {
        return rtn;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("imgui/misc/fonts/Roboto-Medium.ttf", 16.0f);
    io.Fonts->AddFontFromFileTTF("imgui/misc/fonts/ProggyClean.ttf", 20.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    emulator_init();

    // GDB stub init
    static gdb_stub_callbacks_t gdb_cb = {
        gdb_read_reg8, gdb_read_reg16,
        gdb_write_reg8, gdb_write_reg16,
        gdb_read_mem, gdb_write_mem,
        gdb_step_instruction,
        gdb_set_breakpoint, gdb_clear_breakpoint,
        gdb_get_pc, gdb_get_stop_reason,
        gdb_reset, gdb_continue_exec, gdb_halt
    };
    static gdb_stub_config_t gdb_cfg = { 3333, true };
    gdb_stub_init(&gdb_cb, &gdb_cfg);

    // Our state
    bool show_memmap_window = true;
    bool show_status_window = true;
    bool show_console_window = true;

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;

#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!done)
#endif
    {
        static bool show_disasm_window = true;
        static char break_points[128] {0};

        // GDB stub poll
        switch (gdb_stub_poll()) {
            case GDB_POLL_HALTED:
                run_emulator = false;
                gdb_halted = true;
                bp_enable = true;
                emulator_enablebp(true);
                break;
            case GDB_POLL_RESUMED:
                gdb_halted = false;
                run_emulator = true;
                break;
            case GDB_POLL_STEPPED:
                gdb_halted = true;
                run_emulator = false;
                break;
            case GDB_POLL_DETACHED:
                gdb_halted = false;
                // D44: clear all GDB breakpoints on disconnect
                memset(bp_mask, 0, sizeof(bool) * 65536);
                bp_enable = false;
                emulator_enablebp(false);
                break;
            case GDB_POLL_KILL:
                gdb_halted = false;
                run_emulator = true;
                break;
            case GDB_POLL_NONE:
                break;
        }

        uint32_t steps = 0;
        if (run_emulator && !gdb_halted) {
            uint32_t timeout = SDL_GetTicks() + 13;
            while (!SDL_TICKS_PASSED(SDL_GetTicks(), timeout)) {
                emulator_step();
                steps++;
                if (emulator_bp_hit()) {
                    run_emulator = false;
                    emulator_clear_bp_hit();
                    if (gdb_stub_is_connected()) {
                        gdb_halted = true;
                        gdb_stub_notify_stop(5);
                    }
                    break;
                }

            }
        } else if (step_emulator && !gdb_halted) {
            emulator_step();
            steps++;
            step_emulator = false;
        }
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Control Window
        {
            ImGui::Begin("Emulator Control");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Checkbox("CPU", &show_status_window);
            ImGui::SameLine();  ImGui::Checkbox("Disasm", &show_disasm_window);
            ImGui::SameLine();  ImGui::Checkbox("Memory", &show_memmap_window);
            ImGui::SameLine();  ImGui::Checkbox("Console", &show_console_window);
            ImGui::Text("  ");
            if (gdb_halted && gdb_stub_is_connected())
                ImGui::Text("Status: Halted (GDB)");
            else
                ImGui::Text("Status: %s", run_emulator ? "Running" : "Halted");

            if (gdb_stub_is_connected())
                ImGui::Text("GDB: Connected (port 3333)");
            else
                ImGui::Text("GDB: Listening");

            ImGui::BeginDisabled(gdb_halted);
            if(ImGui::Button(run_emulator?"Pause":" Run ")) {
                run_emulator = !run_emulator;
            }
            ImGui::SameLine(80);
            ImGui::BeginDisabled(run_emulator);
            if(ImGui::Button("Step")) {
                step_emulator = true;
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled(); // gdb_halted
            ImGui::SameLine(150);
            if(ImGui::Button("Reset")) {
                emulator_reset();
            }
            ImGui::SameLine(230);
            ImGui::BeginDisabled(gdb_stub_is_connected());
            if(ImGui::Checkbox("BP", &bp_enable)) {
                emulator_enablebp(bp_enable);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(300);
            if(ImGui::InputText("BP2", break_points,IM_ARRAYSIZE(break_points))) {
                emulator_setbp(break_points);
            }

            ImGui::Text("Steps per frame: %d", steps);
            ImGui::Text("Steps per sec: %f:", io.Framerate * steps);
            ImGui::End();
        }

        
        // 3. Show Memory dumpo window.
        if (show_memmap_window) {
            emulator_show_memdump_window(show_memmap_window);
        }

        // Show CPU register status window
        if (show_status_window)
        {
            emulator_show_status_window(show_status_window,1000.0f / io.Framerate,io.Framerate);
        }

        if (show_disasm_window) {
            emu_dis6502_window(show_disasm_window);
        }
        if (show_console_window) {
            emulator_show_console_window(show_console_window);
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call SDL_GL_MakeCurrent(window, gl_context) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
        }

        SDL_GL_SwapWindow(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    gdb_stub_shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
