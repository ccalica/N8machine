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
#include "n8_memory_map.h"
#include "emu_dis6502.h"
#include "machine.h"
#include "utils.h"
#include "gdb_stub.h"
#include "gdb_bridge.h"
#include "emu_video.h"
#include "emu_kbd.h"
#include "emu_display.h"

const char* glsl_version;
SDL_WindowFlags window_flags;
SDL_Window* window;
SDL_GLContext gl_context;

// ---- SDL → N8 keyboard translation ----

static uint8_t sdl_to_n8_keycode(SDL_Keysym keysym) {
    SDL_Keycode key = keysym.sym;
    Uint16 mod = keysym.mod;

    // Ctrl+letter → $01-$1A
    if ((mod & KMOD_CTRL) && key >= SDLK_a && key <= SDLK_z) {
        return (uint8_t)(key - SDLK_a + 1);
    }

    // Special keys
    switch (key) {
        case SDLK_RETURN:    return 0x0D;
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB:       return 0x09;
        case SDLK_ESCAPE:    return 0x1B;
        case SDLK_DELETE:    return 0x87;
        // Arrow keys ($80+)
        case SDLK_UP:        return 0x80;
        case SDLK_DOWN:      return 0x81;
        case SDLK_LEFT:      return 0x82;
        case SDLK_RIGHT:     return 0x83;
        case SDLK_HOME:      return 0x84;
        case SDLK_END:       return 0x85;
        case SDLK_PAGEUP:    return 0x86;
        case SDLK_PAGEDOWN:  return 0x88;
        case SDLK_INSERT:    return 0x89;
        // Function keys ($90+)
        case SDLK_F1:        return 0x90;
        case SDLK_F2:        return 0x91;
        case SDLK_F3:        return 0x92;
        case SDLK_F4:        return 0x93;
        case SDLK_F5:        return 0x94;
        case SDLK_F6:        return 0x95;
        case SDLK_F7:        return 0x96;
        case SDLK_F8:        return 0x97;
        case SDLK_F9:        return 0x98;
        case SDLK_F10:       return 0x99;
        case SDLK_F11:       return 0x9A;
        case SDLK_F12:       return 0x9B;
        default: break;
    }

    // Printable ASCII ($20-$7E)
    if (key >= SDLK_SPACE && key <= SDLK_z) {
        if (mod & KMOD_SHIFT) {
            // Shifted symbols
            switch (key) {
                case SDLK_1: return '!';
                case SDLK_2: return '@';
                case SDLK_3: return '#';
                case SDLK_4: return '$';
                case SDLK_5: return '%';
                case SDLK_6: return '^';
                case SDLK_7: return '&';
                case SDLK_8: return '*';
                case SDLK_9: return '(';
                case SDLK_0: return ')';
                case SDLK_MINUS:        return '_';
                case SDLK_EQUALS:       return '+';
                case SDLK_LEFTBRACKET:  return '{';
                case SDLK_RIGHTBRACKET: return '}';
                case SDLK_BACKSLASH:    return '|';
                case SDLK_SEMICOLON:    return ':';
                case SDLK_QUOTE:        return '"';
                case SDLK_BACKQUOTE:    return '~';
                case SDLK_COMMA:        return '<';
                case SDLK_PERIOD:       return '>';
                case SDLK_SLASH:        return '?';
                default: break;
            }
            // Shift+letter → uppercase
            if (key >= SDLK_a && key <= SDLK_z) {
                return (uint8_t)(key - SDLK_a + 'A');
            }
        }
        // Caps lock + letter → uppercase
        if ((mod & KMOD_CAPS) && key >= SDLK_a && key <= SDLK_z) {
            bool shifted = (mod & KMOD_SHIFT) != 0;
            return shifted ? (uint8_t)key : (uint8_t)(key - SDLK_a + 'A');
        }
        return (uint8_t)key;
    }

    return 0; // Unmapped key
}

static uint8_t sdl_to_n8_modifiers(Uint16 sdl_mod) {
    uint8_t mods = 0;
    if (sdl_mod & KMOD_SHIFT) mods |= N8_KBD_STAT_SHIFT;
    if (sdl_mod & KMOD_CTRL)  mods |= N8_KBD_STAT_CTRL;
    if (sdl_mod & KMOD_ALT)   mods |= N8_KBD_STAT_ALT;
    if (sdl_mod & KMOD_CAPS)  mods |= N8_KBD_STAT_CAPS;
    return mods;
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
    gdb_bridge_init();

    display_init(video_get_screen());

    // Our state
    bool show_memmap_window = true;
    bool show_status_window = true;
    bool show_console_window = true;
    bool show_screen_window = true;

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;

#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    static uint32_t frame_count = 0;
    while (!done)
#endif
    {
        static bool show_disasm_window = true;
        static char break_points[128] {0};

        gdb_bridge_poll();

        uint32_t steps = 0;
        if (emulator_is_running() && !emulator_is_gdb_halted()) {
            uint32_t timeout = SDL_GetTicks() + 13;
            while (!SDL_TICKS_PASSED(SDL_GetTicks(), timeout)) {
                emulator_step();
                steps++;
                if (gdb_bridge_check_stop()) break;
            }
        } else if (emulator_is_stepping() && !emulator_is_gdb_halted()) {
            emulator_step();
            steps++;
            emulator_set_stepping(false);
        }
        video_rasterize(frame_count);
        frame_count++;

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
            if (event.type == SDL_KEYDOWN && !event.key.repeat
                && (!io.WantCaptureKeyboard || display_has_focus())) {
                uint8_t n8_key = sdl_to_n8_keycode(event.key.keysym);
                uint8_t mods   = sdl_to_n8_modifiers(SDL_GetModState());
                if (n8_key != 0) {
                    kbd_inject_key(n8_key, mods);
                }
            }
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
            ImGui::SameLine();  ImGui::Checkbox("Screen", &show_screen_window);
            ImGui::Text("  ");
            if (emulator_is_gdb_halted() && gdb_stub_is_connected())
                ImGui::Text("Status: Halted (GDB)");
            else
                ImGui::Text("Status: %s", emulator_is_running() ? "Running" : "Halted");

            if (gdb_stub_is_connected())
                ImGui::Text("GDB: Connected (port 3333)");
            else
                ImGui::Text("GDB: Listening");

            ImGui::BeginDisabled(emulator_is_gdb_halted());
            if(ImGui::Button(emulator_is_running()?"Pause":" Run ")) {
                emulator_set_running(!emulator_is_running());
            }
            ImGui::SameLine(80);
            ImGui::BeginDisabled(emulator_is_running());
            if(ImGui::Button("Step")) {
                emulator_set_stepping(true);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled(); // gdb_halted
            ImGui::SameLine(150);
            if(ImGui::Button("Reset")) {
                emulator_reset();
            }
            ImGui::SameLine(230);
            {
                bool bp_en = emulator_bp_enabled();
                ImGui::BeginDisabled(gdb_stub_is_connected());
                if(ImGui::Checkbox("BP", &bp_en)) {
                    emulator_enablebp(bp_en);
                }
                ImGui::EndDisabled();
            }
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
        if (show_screen_window) {
            display_render();
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
    display_shutdown();
    gdb_bridge_shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
