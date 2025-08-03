#include "renderer.common.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <tuple>

#include <fmt/core.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <glad/glad.h>
#include <shaderc/shaderc.hpp>
#include <tl/expected.hpp>

#include "error.hpp"

using namespace std;

tl::expected<pair<GLenum, shaderc_shader_kind>, SystemError> classify_shader_file(const filesystem::path& fspath) {
    assert(fspath.has_extension());

    const filesystem::path ext = fspath.extension();
    if (ext == ".vert")
        return {pair{GL_VERTEX_SHADER, shaderc_vertex_shader}};
    if (ext == ".frag")
        return {pair{GL_FRAGMENT_SHADER, shaderc_fragment_shader}};

    return tl::make_unexpected(SystemError{make_error_code(errc::not_supported)});
}

struct shader_log_tag {};
struct program_log_tag {};

template <typename log_tag> void print_log_tag(const GLuint obj) {
    char temp_buff[1024];
    GLsizei log_size{};

    if constexpr (is_same_v<log_tag, shader_log_tag>) {
        glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &log_size);
    } else if constexpr (is_same_v<log_tag, program_log_tag>) {
        glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &log_size);
    }

    if (log_size <= 0) {
        return;
    }

    log_size = min<GLsizei>(log_size, size(temp_buff));

    if constexpr (is_same_v<log_tag, shader_log_tag>) {
        glGetShaderInfoLog(obj, log_size - 1, &log_size, temp_buff);
    } else if constexpr (is_same_v<log_tag, program_log_tag>) {
        glGetProgramInfoLog(obj, log_size - 1, &log_size, temp_buff);
    }

    temp_buff[log_size] = 0;
    LOG_ERROR(g_logger, "[program|shader] {} error:\n{}", obj, temp_buff);
}

tl::expected<GLuint, GenericProgramError>
create_gpu_program_from_memory(const GLenum shader_kind_gl, const shaderc_shader_kind shader_kind_sc,
                               const string_view input_filename, const string_view src_code,
                               const string_view entry_point,
                               const span<const glsl_preprocessor_define> preprocessor_defines, const bool optimize) {
    shaderc::Compiler compiler{};
    shaderc::CompileOptions compile_options{};

    for (const auto [macro_name, macro_val] : preprocessor_defines) {
        compile_options.AddMacroDefinition(macro_name.data(), macro_name.length(), macro_val.data(),
                                           macro_val.length());
    }

    compile_options.SetOptimizationLevel(optimize ? shaderc_optimization_level_performance
                                                  : shaderc_optimization_level_zero);
    compile_options.SetTargetEnvironment(shaderc_target_env_opengl, 0);

    shaderc::PreprocessedSourceCompilationResult preprocessing_result = compiler.PreprocessGlsl(
        src_code.data(), src_code.size(), shader_kind_sc, input_filename.data(), compile_options);

    if (preprocessing_result.GetCompilationStatus() != shaderc_compilation_status_success) {
        LOG_ERROR(g_logger, "Shader {} preprocessing failure:\n{}", input_filename,
                  preprocessing_result.GetErrorMessage());
        return tl::make_unexpected(ShadercError{preprocessing_result.GetErrorMessage()});
    }

    const string_view preprocessed_source{preprocessing_result.begin(), preprocessing_result.end()};
    LOG_INFO(g_logger, "Preprocessed shader:\n{}", preprocessed_source);

    shaderc::SpvCompilationResult compilation_result =
        compiler.CompileGlslToSpv(preprocessed_source.data(), preprocessed_source.length(), shader_kind_sc,
                                  input_filename.data(), compile_options);

    if (compilation_result.GetCompilationStatus() != shaderc_compilation_status_success) {
        LOG_ERROR(g_logger, "Shader [[ {} ]] compilation:: {} error(s)\n{}", input_filename,
                  compilation_result.GetNumErrors(), compilation_result.GetErrorMessage());
        return tl::make_unexpected(ShadercError{compilation_result.GetErrorMessage()});
    }

    span<const uint32_t> spirv_bytecode{compilation_result.begin(),
                                        static_cast<size_t>(compilation_result.end() - compilation_result.begin())};

    const GLuint shader_handle{glCreateShader(shader_kind_gl)};
    glShaderBinary(1, &shader_handle, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv_bytecode.data(),
                   static_cast<GLsizei>(spirv_bytecode.size_bytes()));
    glSpecializeShader(shader_handle, entry_point.data(), 0, nullptr, nullptr);

    GLint compile_status{};
    glGetShaderiv(shader_handle, GL_COMPILE_STATUS, &compile_status);

    if (compile_status != GL_TRUE) {
        print_log_tag<shader_log_tag>(shader_handle);
        glDeleteShader(shader_handle);
        return tl::make_unexpected(OpenGLError{glGetError()});
    }

    const GLuint program_handle{glCreateProgram()};
    glProgramParameteri(program_handle, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(program_handle, shader_handle);
    glLinkProgram(program_handle);

    GLint link_status{};
    glGetProgramiv(program_handle, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        print_log_tag<program_log_tag>(program_handle);
    }

    glDetachShader(program_handle, shader_handle);
    glDeleteShader(shader_handle);

    if (!link_status) {
        glDeleteProgram(program_handle);
        return tl::make_unexpected(OpenGLError{glGetError()});
    }

    return program_handle;
}

tl::expected<GLuint, GenericProgramError>
create_gpu_program_from_file(const filesystem::path& source_file, const string_view entry_point,
                             const span<const glsl_preprocessor_define> preprocessor_defines,
                             const bool optimize = false) {
    error_code e{};
    const auto file_size = filesystem::file_size(source_file, e);
    if (e) {
        LOG_ERROR(g_logger, "FS error {}", e.message());
        return tl::make_unexpected(SystemError{e});
    }

    string shader_code{};
    shader_code.reserve(static_cast<size_t>(file_size) + 1);

    ifstream f{source_file};
    if (!f) {
        LOG_ERROR(g_logger, "Can't open file {}", source_file.string());
        return tl::make_unexpected(SystemError{make_error_code(errc::io_error)});
    }

    shader_code.assign(istreambuf_iterator<char>{f}, istreambuf_iterator<char>{});

    return classify_shader_file(source_file).and_then([&](auto p) {
        const auto [shader_kind_gl, shader_kind_shaderc] = p;
        return create_gpu_program_from_memory(shader_kind_gl, shader_kind_shaderc, source_file.string(), shader_code,
                                              entry_point, preprocessor_defines);
    });
}

char monka_mega_scratch_msg_buffer[4 * 1024 * 1024];

void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
                       const void* userParam) {
    const char* dbg_src_desc = [source]() {
        switch (source) {
        case GL_DEBUG_SOURCE_API:
            return "OpenGL API";
            break;

        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            return "OpenGL Shader Compiler";
            break;

        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
            return "Windowing system";
            break;

        case GL_DEBUG_SOURCE_THIRD_PARTY:
            return "Third party";
            break;

        case GL_DEBUG_SOURCE_APPLICATION:
            return "Application";
            break;

        case GL_DEBUG_SOURCE_OTHER:
        default:
            return "Other";
        }
    }();

#define DESC_TABLE_ENTRY(glid, desc)                                                                                   \
    case glid:                                                                                                         \
        return desc;                                                                                                   \
        break

    const char* msg_type_desc = [type]() {
        switch (type) {
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_ERROR, "error");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, "deprecated behavior");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, "undefined behavior");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_PERFORMANCE, "performance");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_PORTABILITY, "portability");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_MARKER, "marker");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_PUSH_GROUP, "push group");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_POP_GROUP, "pop group");
            DESC_TABLE_ENTRY(GL_DEBUG_TYPE_OTHER, "other");
        default:
            return "other";
            break;
        }
    }();

    const char* severity_desc = [severity]() {
        switch (severity) {
            DESC_TABLE_ENTRY(GL_DEBUG_SEVERITY_HIGH, "high");
            DESC_TABLE_ENTRY(GL_DEBUG_SEVERITY_MEDIUM, "medium");
            DESC_TABLE_ENTRY(GL_DEBUG_SEVERITY_LOW, "low");
            DESC_TABLE_ENTRY(GL_DEBUG_SEVERITY_NOTIFICATION, "notification");
        default:
            return "unknown";
            break;
        }
    }();

    auto result =
        fmt::format_to(monka_mega_scratch_msg_buffer, "[OpenGL debug]\nsource: {}\ntype: {}\nid {}({:#0x})\n{}",
                       dbg_src_desc, msg_type_desc, id, id, message ? message : "no message");
    *result.out = 0;

    if (severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG_ERROR(g_logger, "{}", monka_mega_scratch_msg_buffer);
    } else {
        // LOG_DEBUG(g_logger, "{}", monka_mega_scratch_msg_buffer);
    }
}

tl::expected<BufferMapping, OpenGLError> BufferMapping::create(const GLuint buffer, const GLintptr offset,
                                                               const GLbitfield access, const GLsizei mapping_len) {
    GLsizei maplength{mapping_len};
    if (maplength == 0) {
        glGetNamedBufferParameteriv(buffer, GL_BUFFER_SIZE, &maplength);
    }

    void* mapped_addr = glMapNamedBufferRange(buffer, offset, maplength, access);
    if (!mapped_addr) {
        return tl::unexpected{OpenGLError{glGetError()}};
    }

    return tl::expected<BufferMapping, OpenGLError>{BufferMapping{buffer, offset, maplength, mapped_addr}};
}

tl::expected<Texture, GenericProgramError> Texture::from_memory(const void* pixels, const int32_t width,
                                                                const int32_t height, const int32_t channels,
                                                                const tl::optional<uint32_t> mip_levels) {
    constexpr const pair<GLenum, GLenum> gl_format_pairs[] = {
        {GL_R8, GL_RED}, {GL_RG8, GL_RG}, {GL_RGB8, GL_RGB}, {GL_RGBA8, GL_RGBA}};
    assert(channels >= 1 && channels <= 4);

    Texture texture{};
    texture.internal_fmt = gl_format_pairs[channels - 1].first;
    texture.width = width;
    texture.height = height;

    glCreateTextures(GL_TEXTURE_2D, 1, &texture.handle);
    glTextureStorage2D(texture.handle, mip_levels.value_or(1), texture.internal_fmt, width, height);
    glTextureSubImage2D(texture.handle, 0, 0, 0, width, height, gl_format_pairs[channels - 1].second, GL_UNSIGNED_BYTE,
                        pixels);

    mip_levels.map([texid = texture.handle](const uint32_t) { glGenerateTextureMipmap(texid); });

    return tl::expected<Texture, GenericProgramError>{texture};
}

tl::expected<Texture, GenericProgramError> Texture::from_file(const filesystem::path& path) {
    return tl::make_unexpected(SystemError{make_error_code(errc::io_error)});
    // const string s_path = path.string();
    // int32_t width{};
    // int32_t height{};
    // int32_t channels{};
    //
    // unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
    // stbi_load(s_path.c_str(), &width, &height, &channels, 0),
    //                                                         &stbi_image_free
    //                                                         };
    //
    // if (!pixels) {
    //     return tl::make_unexpected(SystemError{
    //     make_error_code(errc::io_error) });
    // }
    //
    // return from_memory(pixels.get(), width, height, channels,
    // tl::optional<uint32_t>{ 8 });
}
