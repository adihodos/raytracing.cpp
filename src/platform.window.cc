#include "platform.window.hpp"

#include <chrono>
#include <fmt/format.h>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "logging.hpp"
#include "renderer.common.hpp"

PlatformWindow::PlatformWindow(PrivateAccessToken, SDL_Window* window) noexcept : _window{window} {
    assert(window != nullptr);
    SDL_GetWindowSizeInPixels(window, &RenderData.surface_size.x, &RenderData.surface_size.y);
    SDL_GetWindowSize(window, &RenderData.window_size.x, &RenderData.window_size.y);
}

PlatformWindow::~PlatformWindow() {
    assert(_window != nullptr);
    CHECKED_SDL(SDL_DestroyWindow, _window);
}

tl::optional<PlatformWindow> PlatformWindow::create() {
    if (!CHECKED_SDL(SDL_InitSubSystem, SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return tl::nullopt;
    }

    SDL_Window* window{
        CHECKED_SDL(SDL_CreateWindow, "SDL Window", 1600, 1200, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE)};

    if (!window) {
        return tl::nullopt;
    }

    LOG_DEBUG(g_logger, "Window created {:p}", reinterpret_cast<const void*>(window));

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, true);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    SDL_GLContext gl_context = CHECKED_SDL(SDL_GL_CreateContext, window);
    if (!gl_context) {
        return tl::nullopt;
    }

    LOG_INFO(g_logger, "OpenGL context created {:p}", reinterpret_cast<const void*>(gl_context));

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        return tl::nullopt;
    }

    glDebugMessageCallback(gl_debug_callback, nullptr);

    GLint num_shader_binary_formats{};
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &num_shader_binary_formats);

    LOG_INFO(g_logger, "Supported binary formats {}", num_shader_binary_formats);

    if (num_shader_binary_formats > 0) {
        std::vector<GLint> binary_format_names{};
        binary_format_names.resize(num_shader_binary_formats, -1);
        glGetIntegerv(GL_SHADER_BINARY_FORMATS, binary_format_names.data());

        char tmp_buff[512];
        for (const GLint fmt : binary_format_names) {
            tmp_buff[0] = 0;

            switch (fmt) {
            case GL_SHADER_BINARY_FORMAT_SPIR_V_ARB: {
                auto res = fmt::format_to(tmp_buff, "{} {:#x}", STRINGIZE_(GL_SHADER_BINARY_FORMAT_SPIR_V_ARB), fmt);
                *res.out = 0;
            } break;

            default: {
                auto res = fmt::format_to(tmp_buff, "Unknown {:#x}", fmt);
                *res.out = 0;
            } break;
            }

            LOG_INFO(g_logger, "{}", tmp_buff);
        }
    }

    return tl::optional<PlatformWindow>{tl::in_place, PrivateAccessToken{}, window};
}

void PlatformWindow::event_loop() {
    while (!_quitflag) {
        SDL_Event e;

        if (Events.poll_input_start) {
            Events.poll_input_start(PollInputStartEvent{});
        }

        while (SDL_PollEvent(&e)) {
            Events.input_event(e);
        }

        if (Events.poll_input_end) {
            Events.poll_input_end(PollInputEndEvent{});
        }

        SDL_GetWindowSizeInPixels(_window, &RenderData.surface_size.x, &RenderData.surface_size.y);
        SDL_GetWindowSize(_window, &RenderData.window_size.x, &RenderData.window_size.y);

        const DrawParams draw_params{
            RenderData.surface_size.x,
            RenderData.surface_size.y,
            RenderData.window_size.x,
            RenderData.window_size.y,
        };
        Events.render_event(draw_params);

        SDL_GL_SwapWindow(_window);
        // std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
