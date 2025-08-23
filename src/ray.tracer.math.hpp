#pragma once

#include <cmath>
#include <concepts>

#include <glm/geometric.hpp>
#include <glm/ext.hpp>
#include <glm/vec3.hpp>

template <typename T>
    requires std::is_arithmetic_v<T>
T clamp(const T value, const T low, const T high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

inline bool near_zero(const glm::vec3& v) noexcept {
    constexpr float s = 1e-8f;
    return std::fabs(v.x) < s && std::fabs(v.y) < s && std::fabs(v.z) < s;
}

inline glm::vec3 refract(const glm::vec3& uv, const glm::vec3& n, float ei_et) noexcept {
    const float cos_theta = std::fmin(glm::dot(-uv, n), 1.0f);
    const glm::vec3 r_out_perp = ei_et * (uv + cos_theta * n);
    const glm::vec3 r_out_parallel = -std::sqrtf(std::fabs(1.0 - glm::dot(r_out_perp, r_out_perp))) * n;
    return r_out_perp + r_out_parallel;
}
