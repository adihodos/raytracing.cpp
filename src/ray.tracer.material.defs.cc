#include "ray.tracer.material.defs.hpp"

#include <glm/vec3.hpp>

#include "random.number.gen.hpp"
#include "ray.tracer.math.hpp"
#include "ray.tracer.object.defs.hpp"

typedef tl::optional<ScatterRecord> (*ScatterFuncType)(const void*, const Ray& ray_in,
                                                       const IntersectionRecord& int_rec,
                                                       RandomNumberGenerator& randgen) noexcept;

template <typename T>
concept ScatteringMaterial = requires(const T& mtl, RandomNumberGenerator& randgen) {
    {
        mtl.scatter(std::declval<Ray>(), std::declval<IntersectionRecord>(), randgen)
    } noexcept -> std::same_as<tl::optional<ScatterRecord>>;
};

template <typename S>
    requires ScatteringMaterial<S>
tl::optional<ScatterRecord> scatter_dispatch_func(const void* obj, const Ray& ray_in, const IntersectionRecord& int_rec,
                                                  RandomNumberGenerator& randgen) noexcept {
    return static_cast<const S*>(obj)->scatter(ray_in, int_rec, randgen);
}

constexpr ScatterFuncType kFuncDispatchTable[static_cast<uint32_t>(MaterialKind::Count)] = {
    &scatter_dispatch_func<Material_Lambertian>,
};

tl::optional<ScatterRecord> Material_Lambertian::scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                                         RandomNumberGenerator& randgen) const noexcept {
    glm::vec3 scatter_dir = int_rec.Normal + randgen.random_unit_vector();
    if (near_zero(scatter_dir)) {
        scatter_dir = int_rec.Normal;
    }

    return ScatterRecord{
        .Attenuation = Albedo,
        .ScatteredRay = Ray{int_rec.P, scatter_dir},
    };
}

tl::optional<ScatterRecord> Material_Metallic::scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                                       RandomNumberGenerator& randgen) const noexcept {
    glm::vec3 reflected = glm::reflect(ray_in.Direction, int_rec.Normal);
    reflected = glm::normalize(reflected) + Fuzziness * randgen.random_unit_vector();
    if (glm::dot(reflected, int_rec.Normal) > 0.0f) {
        return ScatterRecord{
            .Attenuation = Albedo,
            .ScatteredRay = Ray{int_rec.P, reflected},
        };
    }
    return tl::nullopt;
}

tl::optional<ScatterRecord> Material_Dielectric::scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                                         RandomNumberGenerator& randgen) const noexcept {
    const float eta = int_rec.FrontFace ? (1.0f / RefractionIndex) : RefractionIndex;
    const glm::vec3 unit_dir = glm::normalize(ray_in.Direction);
    const float cos_theta = std::fmin(glm::dot(-unit_dir, int_rec.Normal), 1.0f);
    const float sin_theta = std::sqrtf(1.0f - cos_theta * cos_theta);

    auto schlick_reflectance_fn = [](const float cosine, const float refaction_index) noexcept {
        const float r0 = (1.0f - refaction_index) / (1.0f + refaction_index);
        const float r1 = r0 * r0;

        return r1 + (1.0f - r1) * std::powf((1.0f - cosine), 5.0f);
    };

    glm::vec3 scatter_dir;
    if (const bool cannot_refract = (eta * sin_theta) > 1.0f;
        cannot_refract || schlick_reflectance_fn(cos_theta, eta) > randgen.random_double()) {
        scatter_dir = glm::reflect(unit_dir, int_rec.Normal);
    } else {
        scatter_dir = glm::refract(unit_dir, int_rec.Normal, eta);
    }

    return ScatterRecord{
        .Attenuation = glm::vec3{1.0f},
        .ScatteredRay =
            Ray{
                .Origin = int_rec.P,
                .Direction = scatter_dir,
            },
    };
}

tl::optional<ScatterRecord> Material::scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                              RandomNumberGenerator& randgen) const noexcept {

    switch (this->MatKind) {
    case MaterialKind::Lambertian:
        return this->Lambertian.scatter(ray_in, int_rec, randgen);
        break;

    case MaterialKind::Metallic:
        return this->Metallic.scatter(ray_in, int_rec, randgen);
        break;

    case MaterialKind::Dielectric:
        return this->Dielectric.scatter(ray_in, int_rec, randgen);
        break;

    default:
        assert(false);
        return tl::nullopt;
    }
}
