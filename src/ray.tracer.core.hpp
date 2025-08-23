#pragma once

#include <cstdint>
#include <limits>
#include <memory>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "color.hpp"
#include "interval.hpp"
#include "ray.hpp"
#include "ray.tracer.material.defs.hpp"
#include "ray.tracer.object.defs.hpp"

class RandomNumberGenerator;

struct RayTracingCore {
    uint32_t rts_img_width;
    uint32_t rts_img_height;
    float rts_defocus_angle;
    float rts_viewport_height;
    float rts_viewport_width;
    uint16_t rts_samples_per_pixel;
    uint16_t rts_maxdepth;
    float rts_pixels_sample_scale;
    glm::vec3 rts_pixel_delta_u;
    glm::vec3 rts_pixel_delta_v;
    glm::vec3 rts_pixel00;
    glm::vec3 rts_cam_center;
    glm::vec3 rts_defocus_disk_u;
    glm::vec3 rts_defocus_disk_v;
    HittableObject_Collection rts_world;
    MaterialCollection rts_materials;

    static std::shared_ptr<RayTracingCore> default_setup();

    static glm::vec3 compute_color(const Ray& r, const uint16_t depth, const HittableObject_Collection& world,
                                   const MaterialCollection& materials, RandomNumberGenerator& randgen) noexcept;
    Ray get_ray(const uint32_t x, const uint32_t y, RandomNumberGenerator& randgen) const;
    RGBAColor raytrace_pixel(const uint32_t x, const uint32_t y, RandomNumberGenerator& rand_gen) const;
};
