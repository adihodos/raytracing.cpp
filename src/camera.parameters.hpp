#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>

struct CameraParameters {
    float aspect_ratio;
    uint32_t image_width;
    uint16_t samples_per_pixel;
    uint16_t max_depth;
    float vertical_fov;
    float defocus_angle;
    float focus_distance;
    float lookfrom[3];
    float lookat[3];
    float world_up[3];

    template <typename Ar> void serialize(Ar& ar) {
        ar& YAS_OBJECT_NVP("CameraParameters", ("aspect_ratio", aspect_ratio), ("image_width", image_width),
                           ("samples_per_pixel", samples_per_pixel), ("max_depth", max_depth),
                           ("vertical_fov", vertical_fov), ("defocus_angle", defocus_angle),
                           ("focus_distance", focus_distance), ("lookfrom", lookfrom), ("lookat", lookat),
                           ("world_up", world_up));
    }
};
