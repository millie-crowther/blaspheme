#include "physics/sphere.h"

bool srph_sphere_intersect(const srph_sphere * a, const srph_sphere * b){
    return a->r + b->r >= srph_vec3_distance(&a->c, &b->c);
}

bool srph_sphere_contains(const srph_sphere * a, const srph_sphere * b){
    return a->r >= srph_vec3_distance(&a->c, &b->c) + b->r;
}
