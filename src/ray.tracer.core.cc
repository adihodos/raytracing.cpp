#include "ray.tracer.core.hpp"

#include <glm/common.hpp>
#include <glm/ext.hpp>
#include <numbers>
#include <strong_type/strong_type.hpp>
#include <tuple>

#include <iosfwd>
#include <iostream>
#include <ostream>

#include <rfl.hpp>
#include <rfl/json.hpp>

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

struct SphereDef {
    std::array<float, 3> center;
    float radius;
};

struct AlbedoMatDef {
    std::array<float, 3> albedo;
};

struct DielectricMatDef {
    float refindex;
};

struct MetallicMatDef {
    std::array<float, 3> albedo;
    float fuzzines;
};

using MaterialDef = rfl::TaggedUnion<"material_def", AlbedoMatDef, DielectricMatDef, MetallicMatDef>;

struct WorldDefinition {
    CameraParameters camera{
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
    int32_t a_min{-11};
    int32_t a_max{11};
    int32_t b_min{-11};
    int32_t b_max{11};
    std::array<float, 3> center{0.2f, 0.9f, 0.2f};
    std::array<float, 3> center_offset{4.0f, 0.2f, 0.0f};
    float center_dist_treshold{0.9f};
    float diffuse_material_treshold{0.85};
    float metal_material_treshold{0.95};
    std::vector<std::pair<SphereDef, MaterialDef>> objects{
        {SphereDef{{0.0f, -1000.0f, 0.0f}, 1000.0f}, AlbedoMatDef{0.5f, 0.5f, 0.5f}},
        {SphereDef{{0.0f, 1.0f, 0.0f}, 1.0f}, DielectricMatDef{1.5f}},
        {SphereDef{{-4.0f, -1.0f, 0.0f}, 1.0f}, AlbedoMatDef{0.4f, 0.2f, 0.1f}},
        {SphereDef{{4.0f, -1.0f, 0.0f}, 1.0f}, AlbedoMatDef{0.7f, 0.6f, 0.5f}},
    };
};

glm::vec3 to_vec3(const std::array<float, 3>& a) noexcept { return glm::vec3{a[0], a[1], a[2]}; }

std::tuple<CameraParameters, HittableObject_Collection, MaterialCollection> make_world_spheres() {
    MaterialCollection material_coll;
    HittableObject_Collection world;
    const WorldDefinition world_def = rfl::json::load<WorldDefinition>("data/config/world.config.json").value();

    for (const auto& [sphere_def, mtl_def] : world_def.objects) {
        const Material mtl = rfl::visit(
            [](const auto& mtl_def) {
                using mat_def_type = std::decay_t<decltype(mtl_def)>;
                if constexpr (std::is_same_v<mat_def_type, AlbedoMatDef>) {
                    return Material::make_lambertian(to_vec3(mtl_def.albedo));
                } else if constexpr (std::is_same_v<mat_def_type, DielectricMatDef>) {
                    return Material::make_dielectric(mtl_def.refindex);
                } else if constexpr (std::is_same_v<mat_def_type, MetallicMatDef>) {
                    return Material::make_metallic(to_vec3(mtl_def.albedo), mtl_def.fuzzines);
                } else {
                    static_assert(rfl::always_false_v<mat_def_type>, "Not all cases were covered.");
                }
            },
            mtl_def);

        const MaterialHandleType mtl_handle = material_coll.add(mtl);
        world.add_object(HittableObject::make_sphere(to_vec3(sphere_def.center), sphere_def.radius, mtl_handle));
    }

    RandomNumberGenerator rand_gen{};
    for (int32_t a = world_def.a_min; a < world_def.a_max; ++a) {
        for (int32_t b = world_def.b_min; b < world_def.b_max; ++b) {
            const float choose_mat = rand_gen.random_double();
            const glm::vec3 center{a + 0.9f * rand_gen.random_double(), 0.2f, b + 0.9 * rand_gen.random_double()};

            if ((center - to_vec3(world_def.center_offset)).length() > world_def.center_dist_treshold) {
                MaterialHandleType mtl_handle;

                if (choose_mat < world_def.diffuse_material_treshold) {
                    const glm::vec3 color = rand_gen.random_vector(0.0f, 1.0f) * rand_gen.random_vector(0.0f, 1.0f);
                    mtl_handle = material_coll.add(Material::make_lambertian(color));
                } else if (choose_mat < world_def.metal_material_treshold) {
                    mtl_handle = material_coll.add(Material::make_metallic(rand_gen.random_vector(0.5f, 1.0f),
                                                                           rand_gen.random_double(0.0f, 0.5f)));
                } else {
                    mtl_handle = material_coll.add(Material::make_dielectric(rand_gen.random_double(1.2f, 1.6f)));
                }

                world.add_object(HittableObject::make_sphere(center, 0.2f, mtl_handle));
            }
        }
    }

    return std::tuple{world_def.camera, world, material_coll};
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
        make_camera_frame(to_vec3(cam_params.lookfrom), to_vec3(cam_params.lookat), to_vec3(cam_params.world_up));

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
