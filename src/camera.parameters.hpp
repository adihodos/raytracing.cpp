#pragma once

#include <array>
#include <cstdint>

struct CameraParameters {
    float aspect_ratio;
    uint32_t image_width;
    uint16_t samples_per_pixel;
    uint16_t max_depth;
    float vertical_fov;
    float defocus_angle;
    float focus_distance;
    std::array<float, 3> lookfrom;
    std::array<float, 3> lookat;
    std::array<float, 3> world_up;
};
