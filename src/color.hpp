#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct RGBA {
    union {
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
        uint32_t color;
    };

    RGBA() noexcept = default;

    explicit RGBA(const uint32_t c) noexcept { this->color = c; }
    explicit RGBA(const glm::vec3& c) noexcept : RGBA{glm::vec4{c, 1.0f}} {}
    explicit RGBA(const glm::vec4& c) noexcept {
        this->r = static_cast<uint8_t>(c.r * 255.0f);
        this->g = static_cast<uint8_t>(c.g * 255.0f);
        this->b = static_cast<uint8_t>(c.b * 255.0f);
        this->a = static_cast<uint8_t>(c.a * 255.0f);
    }
};
