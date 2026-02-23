#include "emu_display.h"
#include "imgui.h"
#include <SDL_opengl.h>

static const n8_screen_t* scr = nullptr;
static GLuint tex_id = 0;
static int tex_w = 0;
static int tex_h = 0;
static ImVec4 phosphor = ImVec4(0.0f, 1.0f, 0.4f, 1.0f);  // green phosphor
static bool screen_focused = false;

void display_init(const n8_screen_t* screen) {
    scr = screen;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    tex_w = 0;
    tex_h = 0;
}

void display_render() {
    if (!scr) return;

    // Upload pixels when dirty or texture size changed
    bool size_changed = (scr->width != tex_w || scr->height != tex_h);
    if (scr->dirty || size_changed) {
        glBindTexture(GL_TEXTURE_2D, tex_id);
        if (size_changed) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scr->width, scr->height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, scr->pixels);
            tex_w = scr->width;
            tex_h = scr->height;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, scr->width, scr->height,
                            GL_RGBA, GL_UNSIGNED_BYTE, scr->pixels);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        // Cast away const to clear dirty flag — display owns this transition
        const_cast<n8_screen_t*>(scr)->dirty = false;
    }

    ImGui::Begin("Screen", nullptr, ImGuiWindowFlags_NoNavInputs);
    screen_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::ColorEdit3("Phosphor", &phosphor.x);

    if (tex_w > 0 && tex_h > 0) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale_x = avail.x / (float)tex_w;
        float scale_y = avail.y / (float)tex_h;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale < 1.0f) scale = 1.0f;
        ImVec2 img_size((float)tex_w * scale, (float)tex_h * scale);

        ImVec4 tint(phosphor.x, phosphor.y, phosphor.z, 1.0f);
        ImGui::Image((ImTextureID)(intptr_t)tex_id, img_size,
                     ImVec2(0, 0), ImVec2(1, 1), tint);
    }
    ImGui::End();
}

bool display_has_focus() { return screen_focused; }

void display_shutdown() {
    if (tex_id) {
        glDeleteTextures(1, &tex_id);
        tex_id = 0;
    }
    scr = nullptr;
}
