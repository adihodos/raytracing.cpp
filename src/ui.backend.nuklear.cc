#include "ui.backend.nuklear.hpp"
#include <utility>

#include <SDL3/SDL.h>
#include <shaderc/shaderc.h>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include "renderer.misc.hpp"

using namespace std;

struct nk_sdl_vertex {
    float position[2];
    float uv[2];
    nk_byte col[4];
};

BackendUI::~BackendUI() {
    gl_state.font_atlas.release();
    glDeleteBuffers(size(gl_state.buffers), gl_state.buffers);
    for (const auto prg : gl_state.gpu_programs) {
        glDeleteProgram(prg);
    }
    glDeleteProgramPipelines(1, &gl_state.prog_pipeline);
    glDeleteVertexArrays(1, &gl_state.vao);
    glDeleteSamplers(1, &gl_state.sampler);

    nk_buffer_free(&gl_state.cmds);
    nk_font_atlas_clear(&atlas);

    nk_free(&ctx);
}

tl::expected<BackendUI, GenericProgramError> BackendUI::create(SDL_Window* win) {
    BackendUI backend;
    backend.window = win;

    UIDeviceData& dev = backend.gl_state;

    const uint32_t buffer_sizes[] = {MAX_VERTICES * sizeof(nk_sdl_vertex), MAX_INDICES * sizeof(uint32_t), 1024};
    glCreateBuffers(size(dev.buffers), dev.buffers);
    for (size_t idx = 0; idx < size(dev.buffers); ++idx) {
        glNamedBufferStorage(dev.buffers[idx], buffer_sizes[idx], nullptr, GL_MAP_WRITE_BIT);
    }

    glCreateVertexArrays(1, &dev.vao);
    vertex_array_append_attrib<decltype(nk_sdl_vertex{}.position)>(dev.vao, 0, offsetof(nk_sdl_vertex, position));
    vertex_array_append_attrib<decltype(nk_sdl_vertex{}.uv)>(dev.vao, 1, offsetof(nk_sdl_vertex, uv));
    vertex_array_append_attrib<decltype(nk_sdl_vertex{}.col)>(dev.vao, 2, offsetof(nk_sdl_vertex, col));

    glVertexArrayVertexBuffer(dev.vao, 0, dev.buffers[0], 0, sizeof(nk_sdl_vertex));
    glVertexArrayElementBuffer(dev.vao, dev.buffers[1]);

    static constexpr const char* UI_VERTEX_SHADER = R"#(
    #version 450 core
    layout (location = 0) in vec2 pos;
    layout (location = 1) in vec2 texcoord;
    layout (location = 2) in vec4 color;

    layout (binding = 0) uniform GlobalParams {
        mat4 WorldViewProj;
    };

    layout (location = 0) out gl_PerVertex {
        vec4 gl_Position;
    };

    layout (location = 0) out VS_OUT_FS_IN {
        vec2 uv;
        vec4 color;
    } vs_out;

    void main() {
        vs_out.uv = texcoord;
        vs_out.color = color;
        gl_Position = WorldViewProj * vec4(pos, 0.0f, 1.0f);
    }
    )#";

    constexpr const char* const UI_FRAGMENT_SHADER = R"#(
    #version 450 core

    layout (binding = 0) uniform sampler2D FontAtlas;
    layout (location = 0) in VS_OUT_FS_IN {
        vec2 uv;
        vec4 color;
    } fs_in;
    layout (location = 0) out vec4 FinalFragColor;

    void main() {
        FinalFragColor = fs_in.color * texture(FontAtlas, fs_in.uv);
    }
    )#";

    glCreateProgramPipelines(1, &dev.prog_pipeline);

    constexpr const tuple<const char*, const char*, GLbitfield, GLenum, shaderc_shader_kind, const char*>
        shader_create_data[] = {{UI_VERTEX_SHADER, "main", GL_VERTEX_SHADER_BIT, GL_VERTEX_SHADER,
                                 shaderc_vertex_shader, "ui_vertex_shader"},
                                {UI_FRAGMENT_SHADER, "main", GL_FRAGMENT_SHADER_BIT, GL_FRAGMENT_SHADER,
                                 shaderc_fragment_shader, "ui_fragment_shader"}};

    size_t idx{};
    for (auto [shader_code, entry_point, shader_stage, shader_type, shaderc_kind, shader_id] : shader_create_data) {
        auto shader_prog =
            create_gpu_program_from_memory(shader_type, shaderc_kind, shader_id, shader_code, entry_point, {});
        if (!shader_prog)
            return tl::make_unexpected(shader_prog.error());

        dev.gpu_programs[idx++] = *shader_prog;
        glUseProgramStages(dev.prog_pipeline, shader_stage, *shader_prog);
    }

    {
        nk_font_atlas_begin(&backend.atlas);

        nk_font* zed =
            nk_font_atlas_add_from_file(&backend.atlas, "data/fonts/ZedMonoNerdFontMono-Medium.ttf", 28.0f, nullptr);

        nk_font* default_font = zed;

        /*struct nk_font *droid = nk_font_atlas_add_from_file(atlas, "../../../extra_font/DroidSans.ttf", 14, 0);*/
        /*struct nk_font *roboto = nk_font_atlas_add_from_file(atlas, "../../../extra_font/Roboto-Regular.ttf", 16,
         * 0);*/
        /*struct nk_font *future = nk_font_atlas_add_from_file(atlas, "../../../extra_font/kenvector_future_thin.ttf",
         * 13, 0);*/
        /*struct nk_font *clean = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyClean.ttf", 12, 0);*/
        /*struct nk_font *tiny = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyTiny.ttf", 10, 0);*/
        /*struct nk_font *cousine = nk_font_atlas_add_from_file(atlas, "../../../extra_font/Cousine-Regular.ttf", 13,
         * 0);*/
        int atlas_width{}, atlas_height{};
        const void* atlas_pixels =
            nk_font_atlas_bake(&backend.atlas, &atlas_width, &atlas_height, NK_FONT_ATLAS_RGBA32);

        auto maybe_texture = Texture::from_memory(atlas_pixels, atlas_width, atlas_height, 4);
        if (!maybe_texture)
            return tl::make_unexpected(maybe_texture.error());

        backend.gl_state.font_atlas = *maybe_texture;
        nk_font_atlas_end(&backend.atlas, nk_handle_id((int)backend.gl_state.font_atlas.handle),
                          &backend.gl_state.tex_null);

        // if (backend.atlas.default_font)
        //     nk_style_set_font(&backend.ctx, &backend.atlas.default_font->handle);
        nk_style_set_font(&backend.ctx, &default_font->handle);

        glCreateSamplers(1, &dev.sampler);
        glSamplerParameteri(dev.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(dev.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    backend.time_of_last_frame = SDL_GetTicks();
    return tl::expected<BackendUI, GenericProgramError>{std::move(backend)};
}

bool BackendUI::handle_event(const SDL_Event* evt) {
    struct nk_context* ctx = &this->ctx;

    switch (evt->type) {
    case SDL_EVENT_KEY_UP: /* KEYUP & KEYDOWN share same routine */
    case SDL_EVENT_KEY_DOWN: {
        const bool down = evt->type == SDL_EVENT_KEY_DOWN;
        const bool* state = SDL_GetKeyboardState(0);
        switch (evt->key.key) {
        case SDLK_RSHIFT: /* RSHIFT & LSHIFT share same routine */
        case SDLK_LSHIFT:
            nk_input_key(ctx, NK_KEY_SHIFT, down);
            break;
        case SDLK_DELETE:
            nk_input_key(ctx, NK_KEY_DEL, down);
            break;
        case SDLK_RETURN:
            nk_input_key(ctx, NK_KEY_ENTER, down);
            break;
        case SDLK_TAB:
            nk_input_key(ctx, NK_KEY_TAB, down);
            break;
        case SDLK_BACKSPACE:
            nk_input_key(ctx, NK_KEY_BACKSPACE, down);
            break;
        case SDLK_HOME:
            nk_input_key(ctx, NK_KEY_TEXT_START, down);
            nk_input_key(ctx, NK_KEY_SCROLL_START, down);
            break;
        case SDLK_END:
            nk_input_key(ctx, NK_KEY_TEXT_END, down);
            nk_input_key(ctx, NK_KEY_SCROLL_END, down);
            break;
        case SDLK_PAGEDOWN:
            nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
            break;
        case SDLK_PAGEUP:
            nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
            break;
        case SDLK_Z:
            nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_R:
            nk_input_key(ctx, NK_KEY_TEXT_REDO, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_C:
            nk_input_key(ctx, NK_KEY_COPY, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_V:
            nk_input_key(ctx, NK_KEY_PASTE, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_X:
            nk_input_key(ctx, NK_KEY_CUT, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_B:
            nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_E:
            nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_UP:
            nk_input_key(ctx, NK_KEY_UP, down);
            break;
        case SDLK_DOWN:
            nk_input_key(ctx, NK_KEY_DOWN, down);
            break;
        case SDLK_LEFT:
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
            else
                nk_input_key(ctx, NK_KEY_LEFT, down);
            break;
        case SDLK_RIGHT:
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
            else
                nk_input_key(ctx, NK_KEY_RIGHT, down);
            break;
        }
    }
        return true;

    case SDL_EVENT_MOUSE_BUTTON_UP: /* MOUSEBUTTONUP & MOUSEBUTTONDOWN share same routine */
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const bool down = evt->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const int x = evt->button.x, y = evt->button.y;
        switch (evt->button.button) {
        case SDL_BUTTON_LEFT:
            if (evt->button.clicks > 1)
                nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
            break;
        case SDL_BUTTON_MIDDLE:
            nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
            break;
        case SDL_BUTTON_RIGHT:
            nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
            break;
        }
    }
        return true;

    case SDL_EVENT_MOUSE_MOTION:
        if (ctx->input.mouse.grabbed) {
            int x = (int)ctx->input.mouse.prev.x, y = (int)ctx->input.mouse.prev.y;
            nk_input_motion(ctx, x + evt->motion.xrel, y + evt->motion.yrel);
        } else
            nk_input_motion(ctx, evt->motion.x, evt->motion.y);
        return true;

    case SDL_EVENT_TEXT_INPUT: {
        nk_glyph glyph;
        memcpy(glyph, evt->text.text, NK_UTF_SIZE);
        nk_input_glyph(ctx, glyph);
    }
        return true;

    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2((float)evt->wheel.x, (float)evt->wheel.y));
        return true;

    default:
        return false;
    }
}

void BackendUI::render(const DrawParams& dp) {
    UIDeviceData* dev = &this->gl_state;

    const uint64_t now = SDL_GetTicks();
    ctx.delta_time_seconds = (float)(now - time_of_last_frame) / 1000;
    time_of_last_frame = now;
    const struct nk_vec2 scale{1.0f, 1.0f};

    /* setup global state */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    BufferMapping::create(dev->buffers[2], 0, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT)
        .map([&dp](BufferMapping ubo) {
            const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(dp.surface_width),
                                                    static_cast<float>(dp.surface_height), 0.0f, -1.0f, 1.0f);
            memcpy(ubo.mapped_addr, &projection, sizeof(projection));
        });

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, dev->buffers[2]);
    glBindProgramPipeline(dev->prog_pipeline);
    glBindVertexArray(dev->vao);
    glBindSampler(0, dev->sampler);

    {
        //
        // convert draw commands and fill vertex + index buffer
        {
            auto vertex_buffer =
                BufferMapping::create(dev->buffers[0], 0, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
            auto index_buffer =
                BufferMapping::create(dev->buffers[1], 0, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

            assert(vertex_buffer && index_buffer);

            static const struct nk_draw_vertex_layout_element vertex_layout[] = {
                {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, position)},
                {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, uv)},
                {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_sdl_vertex, col)},
                {NK_VERTEX_LAYOUT_END}};

            nk_convert_config config{};
            config.vertex_layout = vertex_layout;
            config.vertex_size = sizeof(nk_sdl_vertex);
            config.vertex_alignment = NK_ALIGNOF(struct nk_sdl_vertex);
            config.tex_null = dev->tex_null;
            config.circle_segment_count = 22;
            config.curve_segment_count = 22;
            config.arc_segment_count = 22;
            config.global_alpha = 1.0f;
            config.shape_AA = NK_ANTI_ALIASING_ON;
            config.line_AA = NK_ANTI_ALIASING_ON;

            /* setup buffers to load vertices and elements */
            nk_buffer vbuf;
            nk_buffer_init_fixed(&vbuf, vertex_buffer->mapped_addr,
                                 (nk_size)BackendUI::MAX_VERTICES * sizeof(nk_sdl_vertex));

            nk_buffer ebuf;
            nk_buffer_init_fixed(&ebuf, index_buffer->mapped_addr, (nk_size)BackendUI::MAX_INDICES * sizeof(uint32_t));
            nk_convert(&ctx, &dev->cmds, &vbuf, &ebuf, &config);
        }

        /* iterate over and execute each draw command */
        const nk_draw_command* cmd;
        const nk_draw_index* offset = nullptr;
        nk_draw_foreach(cmd, &ctx, &dev->cmds) {
            if (!cmd->elem_count)
                continue;

            glBindTextureUnit(0, static_cast<GLuint>(cmd->texture.id));
            glScissor((GLint)(cmd->clip_rect.x * scale.x),
                      (GLint)((dp.surface_height - (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) * scale.y),
                      (GLint)(cmd->clip_rect.w * scale.x), (GLint)(cmd->clip_rect.h * scale.y));
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count, GL_UNSIGNED_INT, offset);
            offset += cmd->elem_count;
        }

        nk_clear(&ctx);
        nk_buffer_clear(&dev->cmds);
    }

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void BackendUI::input_end() {
    struct nk_context* pctx = &ctx;

    if (pctx->input.mouse.grab) {
        SDL_SetWindowRelativeMouseMode(window, true);
    } else if (pctx->input.mouse.ungrab) {
        /* better support for older SDL by setting mode first; causes an extra mouse motion event */
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_WarpMouseInWindow(window, (int)pctx->input.mouse.prev.x, (int)pctx->input.mouse.prev.y);
    } else if (pctx->input.mouse.grabbed) {
        pctx->input.mouse.pos.x = pctx->input.mouse.prev.x;
        pctx->input.mouse.pos.y = pctx->input.mouse.prev.y;
    }
    nk_input_end(&ctx);
}
