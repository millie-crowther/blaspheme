#ifndef AABB_H
#define AABB_H

#include <memory>

#include "maths/vec.h"
#include "sdf/sdf.h"

class aabb_t {
private:
    // fields
    vec3_t min;
    double size;

public:
    aabb_t();
    aabb_t(const vec3_t & min, double size);

    // modifiers
    void refine(uint8_t octant);

    // accessors
    int get_octant(const vec3_t & x) const;
    vec3_t get_centre() const;
    double get_upper_radius() const;
    double get_size() const;
    std::shared_ptr<sdf_t> get_sdf() const;

    // predicates
    bool contains(const vec3_t & x) const;
};

#endif
