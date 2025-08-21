#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <random>

class RandomNumberGenerator {
public:
    RandomNumberGenerator() = default;

    double random_double() noexcept { return _randdist(_randgen); }
    double random_double(const double r_min, const double r_max) noexcept {
        return r_min + (r_max - r_min) * random_double();
    }

    glm::vec3 sample_square() noexcept { return glm::vec3{random_double() - 0.5f, random_double() - 0.5f, 0.0f}; }
    glm::vec3 random_vector() noexcept { return glm::vec3{random_double(), random_double(), random_double()}; }
    glm::vec3 random_vector(const double rmin, const double rmax) noexcept {
        return glm::vec3{random_double(rmin, rmax), random_double(rmin, rmax), random_double(rmin, rmax)};
    }
    glm::vec3 random_unit_vector() noexcept {
        for (;;) {
            const glm::vec3 p = random_vector(-1.0, 1.0);
            const float length_squared = glm::dot(p, p);
            if (length_squared > 1e-160 && length_squared <= 1.0f) {
                return p / std::sqrt(length_squared);
            }
        }
    }
    glm::vec3 random_vector_on_hemisphere(const glm::vec3& normal) noexcept {
        const glm::vec3 p = random_unit_vector();
        return glm::dot(normal, p) > 0.0f ? p : -p;
    }

private:
    std::random_device _randdev{};
    std::mt19937 _randgen{_randdev()};
    std::uniform_real_distribution<> _randdist{0.0, 1.0};
};
