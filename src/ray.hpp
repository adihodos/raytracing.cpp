#pragma once

#include <glm/vec3.hpp>

struct Ray {
    glm::vec3 Origin;
    glm::vec3 Direction;

    glm::vec3 point_at_param(const float t) const noexcept { return Origin + Direction * t; }
};