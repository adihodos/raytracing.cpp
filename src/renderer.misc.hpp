#pragma once

#include <tuple>
#include <type_traits>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

template <typename T> std::tuple<GLenum, uint32_t, bool> vertex_array_attrib_from_type() {
    using namespace std;

    if constexpr (is_same_v<T, glm::vec2> || is_same_v<remove_cvref_t<T>, float[2]>) {
        return {GL_FLOAT, 2, false};
    } else if constexpr (is_same_v<T, glm::vec3> || is_same_v<remove_cvref_t<T>, float[3]>) {
        return {GL_FLOAT, 3, false};
    } else if constexpr (is_same_v<T, glm::vec4> || is_same_v<remove_cvref_t<T>, float[4]>) {
        return {GL_FLOAT, 4, false};
    } else if constexpr (is_same_v<remove_cvref_t<T>, std::byte[4]> || is_same_v<remove_cvref_t<T>, uint8_t[4]>) {
        return {GL_UNSIGNED_BYTE, 4, true};
    } else {
        static_assert(false, "Unhandled type");
    }
}

template <typename T> void vertex_array_append_attrib(const GLuint vao, const uint32_t idx, const uint32_t offset) {
    glEnableVertexArrayAttrib(vao, idx);
    const auto [attr_type, attr_count, attr_normalized] = vertex_array_attrib_from_type<T>();
    glVertexArrayAttribFormat(vao, idx, attr_count, attr_type, attr_normalized, offset);
    glVertexArrayAttribBinding(vao, idx, 0);
}
