#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <tl/optional.hpp>

#include "interval.hpp"

struct Ray;

struct IntersectionRecord {
    glm::vec3 P;
    glm::vec3 Normal;
    double T;
    bool FrontFace;

    IntersectionRecord(const glm::vec3& p, const glm::vec3& outward_normal, const float t, const Ray& r) noexcept;
};

enum class HittableObjectKind : uint32_t {
    Sphere,
    Count,
};

struct HittableObject_Sphere {
    glm::vec3 Center;
    float Radius;

    tl::optional<IntersectionRecord> intersects(const Ray& r, const Interval ray_t) const;
};

struct HittableObject {
    HittableObjectKind ObjKind;
    union {
        HittableObject_Sphere Sphere;
    };

    static HittableObject make_sphere(const glm::vec3& center, const float radius) noexcept {
        return HittableObject{
            .ObjKind = HittableObjectKind::Sphere,
            .Sphere =
                HittableObject_Sphere{
                    .Center = center,
                    .Radius = radius,
                },
        };
    }

    tl::optional<IntersectionRecord> intersects(const Ray& r, const Interval ray_t) const;
};

class HittableObject_Collection {
public:
    void add_object(const HittableObject& obj) { _objects.push_back(obj); }
    void clear() { _objects.clear(); }
    tl::optional<IntersectionRecord> intersects(const Ray& r, const Interval ray_t) const;

private:
    std::vector<HittableObject> _objects;
};
