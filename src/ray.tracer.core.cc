#include "ray.tracer.core.hpp"

#include <glm/common.hpp>
#include <glm/ext.hpp>
#include <numbers>
#include <strong_type/strong_type.hpp>
#include <tuple>

#include <iosfwd>
#include <iostream>
#include <ostream>

#include "camera.parameters.hpp"
#include "random.number.gen.hpp"
#include "ray.tracer.material.handle.hpp"

std::tuple<CameraParameters, HittableObject_Collection, MaterialCollection> make_world_basic() {
    const float R = std::cos(std::numbers::pi_v<float> * 0.25f);

    MaterialCollection material_coll;
    const MaterialHandleType mtl_blue = material_coll.add(Material::make_lambertian(glm::vec3{0.0f, 0.0f, 1.0f}));
    const MaterialHandleType mtl_red = material_coll.add(Material::make_lambertian(glm::vec3{1.0f, 0.0f, 0.0f}));

    HittableObject_Collection world;
    world.add_object(HittableObject::make_sphere(glm::vec3{-R, 0.0f, -1.0f}, R, mtl_blue));
    world.add_object(HittableObject::make_sphere(glm::vec3{R, 0.0f, -1.0f}, R, mtl_red));

    const auto camera_params = CameraParameters{
        .aspect_ratio = 16.0f / 9.0f,
        .image_width = 800,
        .samples_per_pixel = 100,
        .max_depth = 50,
        .vertical_fov = 20.0f,
        .defocus_angle = 10.0f,
        .focus_distance = 3.4f,
        .lookfrom = {-2.0f, 2.0f, 1.0f},
        .lookat = {0.0f, 0.0f, -1.0f},
        .world_up = {0.0f, 1.0f, 0.0f},
    };

    return std::tuple{camera_params, world, material_coll};
}

std::tuple<CameraParameters, HittableObject_Collection, MaterialCollection> make_world_spheres() {
    MaterialCollection material_coll;
    const MaterialHandleType mtl_ground = material_coll.add(Material::make_lambertian(glm::vec3{0.8f, 0.8f, 0.0f}));
    const MaterialHandleType mtl_center = material_coll.add(Material::make_lambertian(glm::vec3{0.1f, 0.2f, 0.5f}));
    const MaterialHandleType mtl_left = material_coll.add(Material::make_dielectric(1.5f));
    const MaterialHandleType mtl_bubble = material_coll.add(Material::make_dielectric(1.0f / 1.5f));
    const MaterialHandleType mtl_right = material_coll.add(Material::make_metallic(glm::vec3{0.8f, 0.6f, 0.2f}, 1.0f));

    HittableObject_Collection world;
    world.add_object(HittableObject::make_sphere(glm::vec3{0.0f, -100.5f, -1.0f}, 100.0f, mtl_ground));
    world.add_object(HittableObject::make_sphere(glm::vec3{0.0f, 0.0f, -1.2f}, 0.5f, mtl_center));
    world.add_object(HittableObject::make_sphere(glm::vec3{-1.0f, 0.0f, -1.0f}, 0.5f, mtl_left));
    world.add_object(HittableObject::make_sphere(glm::vec3{-1.0f, 0.0f, -1.0f}, 0.4f, mtl_bubble));
    world.add_object(HittableObject::make_sphere(glm::vec3{1.0f, 0.0f, -1.0f}, 0.5f, mtl_right));

    const auto camera_params = CameraParameters{
        .aspect_ratio = 16.0f / 9.0f,
        .image_width = 1200,
        .samples_per_pixel = 100,
        .max_depth = 50,
        .vertical_fov = 20.0f,
        .defocus_angle = 10.0f,
        .focus_distance = 3.4f,
        .lookfrom = {-2.0f, 2.0f, 1.0f},
        .lookat = {0.0f, 0.0f, -1.0f},
        .world_up = {0.0f, 1.0f, 0.0f},
    };

    {
        constexpr size_t flags = yas::mem | yas::text;
        std::string buf{};
        yas::save<flags>(buf, camera_params);

        std::ofstream urmom{"urmom.txt"};
        urmom << buf;
        urmom.flush();
    }

    return std::tuple{camera_params, world, material_coll};
}

struct CameraFrame {
    glm::vec3 Center;
    glm::vec3 U;
    glm::vec3 V;
    glm::vec3 W;
};

CameraFrame make_camera_frame(const glm::vec3& lookfrom, const glm::vec3& lookat, const glm::vec3& world_up) noexcept {
    const glm::vec3 W = glm::normalize(lookfrom - lookat);
    const glm::vec3 U = glm::normalize(glm::cross(world_up, W));
    const glm::vec3 V = glm::cross(W, U);

    return CameraFrame{
        .Center = lookfrom,
        .U = U,
        .V = V,
        .W = W,
    };
}

std::shared_ptr<RayTracingCore> RayTracingCore::default_setup() {
    auto [cam_params, world, mtl_coll] = make_world_spheres();

    const uint32_t image_height =
        static_cast<uint32_t>(static_cast<float>(cam_params.image_width) / cam_params.aspect_ratio);

    const float theta = glm::radians(cam_params.vertical_fov);
    const float h = std::tan(theta * 0.5f);
    const float viewport_height = 2.0f * h * cam_params.focus_distance;
    const float viewport_width = viewport_height * (static_cast<float>(cam_params.image_width) / image_height);

    const CameraFrame cam_frame =
        make_camera_frame(glm::vec3{-2.0f, 2.0f, 1.0f}, glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0.0f, 1.0f, 0.0f});

    const glm::vec3 viewport_u = cam_frame.U * viewport_width;
    const glm::vec3 viewport_v = -cam_frame.V * viewport_height;

    const glm::vec3 pixel_delta_u = viewport_u / static_cast<float>(cam_params.image_width);
    const glm::vec3 pixel_delta_v = viewport_v / static_cast<float>(image_height);

    const glm::vec3 viewport_upper_left =
        cam_frame.Center - cam_params.focus_distance * cam_frame.W - viewport_u * 0.5f - viewport_v * 0.5f;
    const glm::vec3 pixel00_loc = viewport_upper_left + 0.5f * (pixel_delta_u + pixel_delta_v);

    const float defocus_radius = cam_params.focus_distance * std::tan(glm::radians(cam_params.defocus_angle * 0.5f));
    // make_world_basic();

    return std::make_shared<RayTracingCore>(RayTracingCore{
        .rts_img_width = cam_params.image_width,
        .rts_img_height = image_height,
        .rts_defocus_angle = cam_params.defocus_angle,
        .rts_viewport_height = viewport_height,
        .rts_viewport_width = viewport_width,
        .rts_samples_per_pixel = cam_params.samples_per_pixel,
        .rts_maxdepth = cam_params.max_depth,
        .rts_pixels_sample_scale = 1.0f / static_cast<float>(cam_params.samples_per_pixel),
        .rts_pixel_delta_u = pixel_delta_u,
        .rts_pixel_delta_v = pixel_delta_v,
        .rts_pixel00 = pixel00_loc,
        .rts_cam_center = cam_frame.Center,
        .rts_defocus_disk_u = cam_frame.U * defocus_radius,
        .rts_defocus_disk_v = cam_frame.V * defocus_radius,
        .rts_world = std::move(world),
        .rts_materials = std::move(mtl_coll),
    });
}

Ray RayTracingCore::get_ray(const uint32_t x, const uint32_t y, RandomNumberGenerator& randgen) const {
    const glm::vec3 pixel_offset = randgen.sample_square();
    const glm::vec3 pixel_sample = rts_pixel00 + (static_cast<float>(x) + pixel_offset.x) * rts_pixel_delta_u +
                                   (static_cast<float>(y) + pixel_offset.y) * rts_pixel_delta_v;

    auto defocus_disk_sample_fn = [this](RandomNumberGenerator& randgen) {
        const glm::vec3 p = randgen.random_vector_on_unit_disk();
        return rts_cam_center + p.x * rts_defocus_disk_u + p.y * rts_defocus_disk_v;
    };

    const glm::vec3 ray_origin = rts_defocus_angle <= 0.0f ? rts_cam_center : defocus_disk_sample_fn(randgen);

    return Ray{
        .Origin = ray_origin,
        .Direction = pixel_sample - ray_origin,
    };
}

glm::vec3 RayTracingCore::compute_color(const Ray& r, const uint16_t depth, const HittableObject_Collection& world,
                                        const MaterialCollection& materials, RandomNumberGenerator& randgen) noexcept {
    if (depth == 0) {
        return glm::vec3{0.0f};
    }

    if (const tl::optional<IntersectionRecord> int_rec =
            world.intersects(r, Interval{0.0001, std::numeric_limits<double>::infinity()})) {

        const Material& material = materials[int_rec->Material];
        if (const tl::optional<ScatterRecord> scatter_rec = material.scatter(r, *int_rec, randgen)) {
            return scatter_rec->Attenuation *
                   compute_color(scatter_rec->ScatteredRay, depth - 1, world, materials, randgen);
        }

        return glm::vec3{0};
    }

    const glm::vec3 unit_dir = glm::normalize(r.Direction);
    const float t = 0.5f * (unit_dir.y + 1.0f);
    return (1.0f - t) * glm::vec3{1.0f} + t * glm::vec3{0.5f, 0.7f, 1.0f};
}

RGBAColor RayTracingCore::raytrace_pixel(const uint32_t x, const uint32_t y, RandomNumberGenerator& rand_gen) const {
    glm::vec3 pixel_color{0.0f};
    for (uint32_t sample = 0; sample < rts_samples_per_pixel; ++sample) {
        pixel_color += compute_color(get_ray(x, y, rand_gen), rts_maxdepth, rts_world, rts_materials, rand_gen);
    }
    return RGBAColor{pixel_color * rts_pixels_sample_scale};
}
