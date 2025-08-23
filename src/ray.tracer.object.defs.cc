#include "ray.tracer.object.defs.hpp"

#include <algorithm>
#include <cassert>
#include <ranges>

#include <glm/glm.hpp>

#include "ray.hpp"

IntersectionRecord::IntersectionRecord(const glm::vec3& p, const glm::vec3& outward_normal, const float t, const Ray& r,
                                       MaterialHandleType mtl) noexcept {
    this->P = p;
    this->T = t;
    this->Material = mtl;
    this->FrontFace = glm::dot(r.Direction, outward_normal) < 0.0f;
    this->Normal = this->FrontFace ? outward_normal : -outward_normal;
}

typedef tl::optional<IntersectionRecord> (*IntersectFuncType)(const void*, const Ray&, const Interval);

template <typename T>
concept SupportsRayIntersection = requires(T a) {
    { a.intersects(Ray{}, Interval{}) } -> std::same_as<tl::optional<IntersectionRecord>>;
};

template <typename T>
    requires SupportsRayIntersection<T>
tl::optional<IntersectionRecord> intersect_dispatch_func(const void* obj, const Ray& r, const Interval ray_t) {
    return static_cast<const T*>(obj)->intersects(r, ray_t);
}

constexpr IntersectFuncType kFuncTable[static_cast<uint32_t>(HittableObjectKind::Count)] = {
    &intersect_dispatch_func<HittableObject_Sphere>,
};

tl::optional<IntersectionRecord> HittableObject::intersects(const Ray& r, const Interval ray_t) const {
    return kFuncTable[static_cast<uint32_t>(this->ObjKind)](&this->Sphere, r, ray_t);
}

tl::optional<IntersectionRecord> HittableObject_Sphere::intersects(const Ray& r, const Interval ray_t) const {

    const glm::vec oc = Center - r.Origin;
    const float a = glm::dot(r.Direction, r.Direction);
    const float h = glm::dot(r.Direction, oc);
    const float c = glm::dot(oc, oc) - Radius * Radius;

    const float delta = h * h - a * c;
    if (delta < 0.0f) {
        return tl::nullopt;
    }

    const float sqrtd = std::sqrt(delta);
    float root = (h - sqrtd) / a;
    if (!ray_t.surrounds(root)) {
        root = (h + sqrtd) / a;
        if (!ray_t.surrounds(root)) {
            return tl::nullopt;
        }
    }

    const glm::vec3 p = r.point_at_param(root);
    const glm::vec3 outward_normal = (p - Center) / Radius;

    return IntersectionRecord{p, outward_normal, root, r, Material};
}

tl::optional<IntersectionRecord> HittableObject_Collection::intersects(const Ray& r, const Interval ray_t) const {
    double closest_object = ray_t.Max;
    tl::optional<IntersectionRecord> intersection;

    for (const HittableObject& obj : _objects) {
        if (const tl::optional<IntersectionRecord> obj_intersect =
                obj.intersects(r, Interval{ray_t.Min, closest_object})) {
            intersection = obj_intersect;
            closest_object = obj_intersect->T;
        }
    }

    return intersection;
}
