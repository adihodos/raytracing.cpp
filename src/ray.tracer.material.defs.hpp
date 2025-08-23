#pragma once

#include <cassert>
#include <cstdint>
#include <glm/vec3.hpp>
#include <tl/optional.hpp>
#include <vector>

#include "ray.hpp"
#include "ray.tracer.material.handle.hpp"

struct IntersectionRecord;
class RandomNumberGenerator;

struct ScatterRecord {
    glm::vec3 Attenuation;
    Ray ScatteredRay;
};

enum class MaterialKind : uint32_t {
    Lambertian,
    Metallic,
    Dielectric,
    Count,
};

struct Material_Lambertian {
    glm::vec3 Albedo;

    tl::optional<ScatterRecord> scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                        RandomNumberGenerator& randgen) const noexcept;
};

struct Material_Metallic {
    glm::vec3 Albedo;
    float Fuzziness;

    tl::optional<ScatterRecord> scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                        RandomNumberGenerator& randgen) const noexcept;
};

struct Material_Dielectric {
    float RefractionIndex;

    tl::optional<ScatterRecord> scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                        RandomNumberGenerator& randgen) const noexcept;
};

struct Material {
    MaterialKind MatKind;
    union {
        Material_Lambertian Lambertian;
        Material_Metallic Metallic;
        Material_Dielectric Dielectric;
    };

    static Material make_lambertian(glm::vec3 albedo) noexcept {
        return Material{
            .MatKind = MaterialKind::Lambertian,
            .Lambertian =
                Material_Lambertian{
                    .Albedo = albedo,
                },
        };
    }

    static Material make_metallic(glm::vec3 albedo, const float fuzziness) noexcept {
        return Material{
            .MatKind = MaterialKind::Metallic,
            .Metallic =
                Material_Metallic{
                    .Albedo = albedo,
                    .Fuzziness = std::min(1.0f, fuzziness),
                },
        };
    }

    static Material make_dielectric(const float refraction_index) noexcept {
        return Material{
            .MatKind = MaterialKind::Dielectric,
            .Dielectric =
                Material_Dielectric{
                    .RefractionIndex = refraction_index,
                },
        };
    }

    tl::optional<ScatterRecord> scatter(const Ray& ray_in, const IntersectionRecord& int_rec,
                                        RandomNumberGenerator& randgen) const noexcept;
};

class MaterialCollection {
public:
    MaterialCollection() = default;

    MaterialHandleType add(const Material& mtl) {
        const MaterialHandleType mtl_handle{static_cast<uint32_t>(_materials.size())};
        _materials.push_back(mtl);
        return mtl_handle;
    }

    const Material& operator[](const MaterialHandleType mtl) const {
        const uint32_t idx = value_of(mtl);
        assert(idx < _materials.size());
        return _materials[idx];
    }

private:
    std::vector<Material> _materials;
};
