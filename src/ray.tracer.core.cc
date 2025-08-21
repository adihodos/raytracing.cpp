#include "ray.tracer.core.hpp"
#include "random.number.gen.hpp"

std::shared_ptr<RayTracingCore> RayTracingCore::default_setup() {
    constexpr float aspect_ratio = 16.0f / 9.0f;
    constexpr uint32_t image_width = 800;
    constexpr uint32_t image_height = static_cast<uint32_t>(static_cast<float>(image_width) / aspect_ratio);

    constexpr float focal_length = 1.0f;
    constexpr float viewport_height = 2.0f;
    constexpr float viewport_width = viewport_height * (static_cast<float>(image_width) / image_height);

    const glm::vec3 camera_center = glm::vec3{0.0f};

    const glm::vec3 viewport_u{viewport_width, 0.0f, 0.0f};
    const glm::vec3 viewport_v{0.0f, -viewport_height, 0.0f};

    const glm::vec3 pixel_delta_u = viewport_u / static_cast<float>(image_width);
    const glm::vec3 pixel_delta_v = viewport_v / static_cast<float>(image_height);

    const glm::vec3 viewport_upper_left =
        camera_center - glm::vec3{0.0f, 0.0f, focal_length} - viewport_u * 0.5f - viewport_v * 0.5f;
    const glm::vec3 pixel00_loc = viewport_upper_left + 0.5f * (pixel_delta_u + pixel_delta_v);

    HittableObject_Collection world;
    world.add_object(HittableObject::make_sphere(glm::vec3{0.0f, 0.0f, -1.0f}, 0.5f));
    world.add_object(HittableObject::make_sphere(glm::vec3{0.0f, -100.5f, -1.0f}, 100.0f));

    return std::make_shared<RayTracingCore>(RayTracingCore{
        .rts_aspect_ratio = aspect_ratio,
        .rts_img_width = image_width,
        .rts_img_height = image_height,
        .rts_focal_length = focal_length,
        .rts_viewport_height = viewport_height,
        .rts_viewport_width = viewport_width,
        .rts_samples_per_pixel = 100,
        .rts_maxdepth = 50,
        .rts_pixels_sample_scale = 1.0f / 100.0f,
        .rts_pixel_delta_u = pixel_delta_u,
        .rts_pixel_delta_v = pixel_delta_v,
        .rts_pixel00 = pixel00_loc,
        .rts_cam_center = camera_center,
        .rts_world = std::move(world),
    });
}

Ray RayTracingCore::get_ray(const uint32_t x, const uint32_t y, RandomNumberGenerator& randgen) const {
    const glm::vec3 pixel_offset = randgen.sample_square();
    const glm::vec3 pixel_sample = rts_pixel00 + (static_cast<float>(x) + pixel_offset.x) * rts_pixel_delta_u +
                                   (static_cast<float>(y) + pixel_offset.y) * rts_pixel_delta_v;
    return Ray{
        .Origin = rts_cam_center,
        .Direction = pixel_sample - rts_cam_center,
    };
}

glm::vec3 RayTracingCore::compute_color(const Ray& r, const uint16_t depth, const HittableObject_Collection& world,
                                        RandomNumberGenerator& randgen) noexcept {
    if (depth == 0) {
        return glm::vec3{0.0f};
    }

    if (const tl::optional<IntersectionRecord> int_rec =
            world.intersects(r, Interval{0.0001, std::numeric_limits<double>::infinity()})) {
        // const glm::vec3 direction = randgen.random_vector_on_hemisphere(int_rec->Normal);
        const glm::vec3 direction = int_rec->Normal + randgen.random_unit_vector();
        return 0.5f * compute_color(Ray{int_rec->P, direction}, depth - 1, world, randgen);
    }

    const glm::vec3 unit_dir = glm::normalize(r.Direction);
    const float t = 0.5f * (unit_dir.y + 1.0f);
    return (1.0f - t) * glm::vec3{1.0f} + t * glm::vec3{0.5f, 0.7f, 1.0f};
}

RGBAColor RayTracingCore::raytrace_pixel(const uint32_t x, const uint32_t y, RandomNumberGenerator& rand_gen) const {
    glm::vec3 pixel_color{0.0f};
    for (uint32_t sample = 0; sample < rts_samples_per_pixel; ++sample) {
        pixel_color += compute_color(get_ray(x, y, rand_gen), rts_maxdepth, rts_world, rand_gen);
    }
    return RGBAColor{pixel_color * rts_pixels_sample_scale};
}
