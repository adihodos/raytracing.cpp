#pragma once

#include <cstdint>
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <tl/optional.hpp>

#include "color.hpp"

struct alignas(16) RayTracedImageSSBOData {
    uint32_t rti_width;
    uint32_t rti_height;
    RGBAColor rti_pixels[1];
};

class RayTracedImageDisplay {
private:
    struct PrivateConstructionToken {};

public:
    static tl::optional<RayTracedImageDisplay> create(glm::uvec2 surface_size, glm::uvec2 img_size);

    RayTracedImageDisplay(glm::uvec2 surface_size, glm::uvec2 img_size, GLuint vao, GLuint pipeline, GLuint vertex_prog,
                          GLuint frag_prog, GLuint ssbo, RayTracedImageSSBOData* ssbo_ptr) noexcept
        : rtid_surface_size{surface_size}, rtid_image_size{img_size}, rtid_vao{vao}, rtid_pipeline{pipeline},
          rtid_vertexprog{vertex_prog}, rtid_fragprog{frag_prog}, rtid_pixelsbuffer{ssbo}, rtid_ssboptr{ssbo_ptr} {}

    RayTracedImageDisplay(RayTracedImageDisplay&& rhs) noexcept
        : rtid_surface_size{rhs.rtid_surface_size}, rtid_image_size{rhs.rtid_image_size},
          rtid_vao{std::exchange(rhs.rtid_vao, 0)}, rtid_pipeline{std::exchange(rhs.rtid_pipeline, 0)},
          rtid_vertexprog{std::exchange(rhs.rtid_vertexprog, 0)}, rtid_fragprog{std::exchange(rhs.rtid_fragprog, 0)},
          rtid_pixelsbuffer{std::exchange(rhs.rtid_pixelsbuffer, 0)},
          rtid_ssboptr{std::exchange(rhs.rtid_ssboptr, nullptr)} {}

    RayTracedImageDisplay(const RayTracedImageDisplay&) = delete;
    RayTracedImageDisplay& operator=(const RayTracedImageDisplay&) = delete;

    ~RayTracedImageDisplay() noexcept {
        if (rtid_vao)
            glDeleteVertexArrays(1, &rtid_vao);
        if (rtid_pipeline)
            glDeleteProgramPipelines(1, &rtid_pipeline);
    }

    glm::uvec2 surface_size() const noexcept { return rtid_image_size; }

    void write_pixel(const uint32_t x, const uint32_t y, const RGBAColor color);

    void draw() {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rtid_pixelsbuffer);
        glBindProgramPipeline(rtid_pipeline);
        glBindVertexArray(rtid_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    }

private:
    glm::uvec2 rtid_surface_size{};
    glm::uvec2 rtid_image_size{};
    GLuint rtid_vao{};
    GLuint rtid_pipeline{};
    GLuint rtid_vertexprog{};
    GLuint rtid_fragprog{};
    GLuint rtid_pixelsbuffer{};
    RayTracedImageSSBOData* rtid_ssboptr{};
};
