#pragma once

#include <cstring>
#include <cstdint>

#include <glad/glad.h>
#include <tl/expected.hpp>

#include "renderer.common.hpp"
#include "nuklear.h"

struct SDL_Window;
union SDL_Event;

struct UIContext
{
    struct nk_context* ctx;
};

struct BackendUI
{
    SDL_Window* window{};
    struct UIDeviceData
    {
        nk_buffer cmds;
        nk_draw_null_texture tex_null;
        GLuint buffers[3]{}; // vertex + index + uniform
        GLuint vao{};
        GLuint gpu_programs[2]{};
        GLuint prog_pipeline{};
        Texture font_atlas{};
        GLuint sampler;
    } gl_state{};
    nk_context ctx;
    nk_font_atlas atlas;
    nk_font* default_font{};
    uint64_t time_of_last_frame{};

    static constexpr const uint32_t MAX_VERTICES = 8192;
    static constexpr const uint32_t MAX_INDICES = 65535;

    BackendUI()
    {
        nk_buffer_init_default(&gl_state.cmds);
        nk_init_default(&ctx, nullptr);
        nk_font_atlas_init_default(&atlas);
    }

    ~BackendUI();

    BackendUI(const BackendUI&) = delete;
    BackendUI& operator=(const BackendUI&) = delete;

    BackendUI(BackendUI&& rhs) noexcept
    {
        memcpy(this, &rhs, sizeof(*this));
        rhs.window = nullptr;
        memset(&rhs.gl_state, 0, sizeof(rhs.gl_state));
        nk_buffer_init_default(&rhs.gl_state.cmds);
        nk_init_default(&rhs.ctx, nullptr);
        nk_font_atlas_init_default(&rhs.atlas);
    }

    static tl::expected<BackendUI, GenericProgramError> create(SDL_Window* win);
    bool handle_event(const SDL_Event* e);

    auto new_frame() -> UIContext { return UIContext{ .ctx = &ctx }; }
    void input_begin() { nk_input_begin(&ctx); }
    void input_end();
    void render(const DrawParams& dp);
};

