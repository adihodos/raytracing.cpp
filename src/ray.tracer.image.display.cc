#include "ray.tracer.image.display.hpp"

#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <shaderc/shaderc.h>

#include "renderer.common.hpp"

extern quill::Logger* g_logger;

tl::optional<RayTracedImageDisplay> RayTracedImageDisplay::create(glm::uvec2 surface_size, glm::uvec2 img_size) {
    GLuint pixel_buffer{};
    glCreateBuffers(1, &pixel_buffer);
    const GLsizeiptr buffer_size =
        static_cast<GLsizeiptr>(sizeof(RayTracedImageSSBOData) + surface_size.x * surface_size.y * sizeof(RGBAColor));
    glNamedBufferStorage(pixel_buffer, buffer_size, nullptr,
                         GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    RayTracedImageSSBOData* gpu_buffer = static_cast<RayTracedImageSSBOData*>(glMapNamedBufferRange(
        pixel_buffer, 0, buffer_size, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT));

    if (!gpu_buffer) {
        LOG_CRITICAL(g_logger, "Failed to map GPU buffer for image data!");
        return tl::nullopt;
    }

    gpu_buffer->rti_height = surface_size.y;
    gpu_buffer->rti_width = surface_size.x;

    GLuint vao{};
    glCreateVertexArrays(1, &vao);

    static constexpr const char* VTX_SHADER_CODE = R"#(
#version 460 core
//
// see https://trass3r.github.io/coding/2019/09/11/bufferless-rendering.html

layout (location = 0) out gl_PerVertex {
vec4 gl_Position;
};

void main() {
const vec2 pos = vec2(gl_VertexID % 2, gl_VertexID / 2) * 4.0 - 1.0;
gl_Position = vec4(pos, 0.0, 1.0);
}
)#";

    constexpr const char* const FRAG_SHADER_CODE = R"#(
#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable

layout (std430, binding = 0) readonly buffer RayTracedImageSSBO {
uint width;
uint height;
uint pixels[];
};

layout (location = 0) out vec4 FinalFragColor;

void main() {
const uint x = uint(gl_FragCoord.x);
const uint y = uint(gl_FragCoord.y);
const uint pixel_idx = y * width + x;
const uint pixel_color = pixels[pixel_idx];
FinalFragColor = unpackUnorm4x8(pixel_color);
}
)#";

    GLuint pipeline{};
    glCreateProgramPipelines(1, &pipeline);
    GLuint gpu_programs[2]{};

    constexpr const std::tuple<const char*, const char*, GLbitfield, GLenum, shaderc_shader_kind, const char*>
        shader_create_data[] = {
            {
                VTX_SHADER_CODE,
                "main",
                GL_VERTEX_SHADER_BIT,
                GL_VERTEX_SHADER,
                shaderc_vertex_shader,
                "ui_vertex_shader",
            },
            {
                FRAG_SHADER_CODE,
                "main",
                GL_FRAGMENT_SHADER_BIT,
                GL_FRAGMENT_SHADER,
                shaderc_fragment_shader,
                "ui_fragment_shader",
            },
        };

    size_t idx{};
    for (auto [shader_code, entry_point, shader_stage, shader_type, shaderc_kind, shader_id] : shader_create_data) {
        auto shader_prog =
            create_gpu_program_from_memory(shader_type, shaderc_kind, shader_id, shader_code, entry_point, {});
        if (!shader_prog)
            return tl::nullopt;

        gpu_programs[idx++] = *shader_prog;
        glUseProgramStages(pipeline, shader_stage, *shader_prog);
    }

    return tl::optional<RayTracedImageDisplay>{
        tl::in_place, surface_size, img_size, vao, pipeline, gpu_programs[0], gpu_programs[1], pixel_buffer, gpu_buffer,
    };
}

void RayTracedImageDisplay::write_pixel(const uint32_t x, const uint32_t y, const RGBAColor color) {
    //
    // translate image to be in the center of the rendered surface
    const glm::uvec2 translation = (rtid_surface_size - rtid_image_size) / 2u;
    const glm::uvec2 pixel_coords = glm::uvec2{x, y} + translation;

    //
    // convert to OpenGL view coords (lower left origin)
    rtid_ssboptr->rti_pixels[(rtid_surface_size.y - 1 - pixel_coords.y) * rtid_surface_size.x + pixel_coords.x] = color;
}
