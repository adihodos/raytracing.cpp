#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>

#include <glad/glad.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <shaderc/shaderc.h>
#include <tl/expected.hpp>
#include <tl/optional.hpp>
#include <type_traits>

#include "error.hpp"
#include "misc.things.hpp"

extern quill::Logger* g_logger;

template <typename error_func, typename called_function, typename... called_func_args>
auto wrap_graphics_call(const char* function_name, error_func err_func, called_function function,
                        called_func_args&&... func_args) -> std::invoke_result_t<called_function, called_func_args...>
    requires std::is_invocable_v<error_func> && std::is_invocable_v<called_function, called_func_args...>
{
    using call_result_type = std::invoke_result_t<called_function, called_func_args...>;
    if constexpr (std::is_same_v<call_result_type, void>) {
        std::invoke(function, std::forward<called_func_args>(func_args)...);
    } else {
        const call_result_type func_result = std::invoke(function, std::forward<called_func_args>(func_args)...);
        if (!func_result) {
            LOG_ERROR(g_logger, "{} error: {}", function_name, err_func());
        }
        return func_result;
    }
}

#define CHECKED_SDL(sdl_func, ...) wrap_graphics_call(STRINGIZE_(sdl_func), SDL_GetError, sdl_func, ##__VA_ARGS__)
#define CHECKED_OPENGL(opengl_func, ...)                                                                               \
    func_call_wrapper(STRINGIZE_(opengl_func), glGetError, opengl_func, ##__VA_ARGS__)

struct BufferMapping {
    GLuint handle;
    GLintptr offset;
    GLsizei length;
    void* mapped_addr;

    static tl::expected<BufferMapping, OpenGLError> create(const GLuint buffer, const GLintptr offset,
                                                           const GLbitfield access, const GLsizei mapping_len = 0);

    BufferMapping(const BufferMapping&) = delete;
    BufferMapping& operator=(const BufferMapping&) = delete;

    BufferMapping(const GLuint buf, const GLintptr off, const GLsizei len, void* mem) noexcept
        : handle{buf}, offset{off}, length{len}, mapped_addr{mem} {}

    ~BufferMapping() {
        if (mapped_addr) {
            // CHECKED_OPENGL(glFlushMappedNamedBufferRange, handle, offset,
            // length);
            glUnmapNamedBuffer(handle);
        }
    }

    BufferMapping(BufferMapping&& rhs) noexcept {
        memcpy(this, &rhs, sizeof(*this));
        memset(&rhs, 0, sizeof(rhs));
    }
};

struct Texture {
    GLuint handle{};
    GLenum internal_fmt{};
    int32_t width{};
    int32_t height{};
    int32_t depth{1};

    static tl::expected<Texture, GenericProgramError> from_file(const std::filesystem::path& path);
    static tl::expected<Texture, GenericProgramError>
    from_memory(const void* pixels, const int32_t width, const int32_t height, const int32_t channels,
                const tl::optional<uint32_t> mip_levels = tl::nullopt);

    void release() noexcept {
        if (handle)
            glDeleteTextures(1, &handle);
    }
};

struct VertexFormatDescriptor {
    int32_t size;
    uint32_t type;
    uint32_t offset;
    bool normalized;
};

class ArcballCamera;

struct DrawParams {
    int32_t surface_width;
    int32_t surface_height;
    int32_t display_width;
    int32_t display_height;
    ArcballCamera* cam;
};

struct DrawElementsIndirectCommand {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLuint baseVertex;
    GLuint baseInstance;
};

void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
                       const void* userParam);

using glsl_preprocessor_define = std::pair<std::string_view, std::string_view>;

tl::expected<GLuint, GenericProgramError> create_gpu_program_from_memory(
    const GLenum shader_kind_gl, const shaderc_shader_kind shader_kind_sc, const std::string_view input_filename,
    const std::string_view src_code, const std::string_view entry_point,
    const std::span<const glsl_preprocessor_define> preprocessor_defines, const bool optimize = false);
