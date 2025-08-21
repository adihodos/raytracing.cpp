#pragma once

#include <cstdint>
#include <cmath>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "ray.tracer.math.hpp"

inline float linear_to_gamma(const float value) noexcept {
    if (value > 0.0f)
        return std::sqrt(value);

    return 0.0f;
}

struct RGBAColor {
    union {
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
        uint32_t color;
    };

    RGBAColor() noexcept = default;

    explicit RGBAColor(const uint32_t c) noexcept { this->color = c; }
    explicit RGBAColor(const glm::vec3& c) noexcept : RGBAColor{glm::vec4{c, 1.0f}} {}
    explicit RGBAColor(const glm::vec4& c) noexcept {
        this->r = static_cast<uint8_t>(clamp(linear_to_gamma(c.r), 0.0f, 0.999f) * 256.0f);
        this->g = static_cast<uint8_t>(clamp(linear_to_gamma(c.g), 0.0f, 0.999f) * 256.0f);
        this->b = static_cast<uint8_t>(clamp(linear_to_gamma(c.b), 0.0f, 0.999f) * 256.0f);
        this->a = static_cast<uint8_t>(clamp(c.a, 0.0f, 0.999f) * 256.0f);
    }
};
