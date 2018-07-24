#include "sdf.h"

constexpr float sdf_t::epsilon;

sdf_t::sdf_t(std::function<float(const vec3_t&)> phi){
    this->phi = phi;
}

float
sdf_t::distance(const vec3_t& p) const {
    return phi(p);
}

vec3_t
sdf_t::normal(const vec3_t& p) const {
    return vec3_t({
        phi(p + vec3_t({ epsilon, 0, 0 })) - phi(p - vec3_t({ epsilon, 0, 0 })),      
        phi(p + vec3_t({ 0, epsilon, 0 })) - phi(p - vec3_t({ 0, epsilon, 0 })),      
        phi(p + vec3_t({ 0, 0, epsilon })) - phi(p - vec3_t({ 0, 0, epsilon }))
    }).normalise();
}

bounds_t
sdf_t::get_bounds(){
    static void (*helper)(const bounds_t&, bounds_t *, const sdf_t& sdf);
    helper = [](const bounds_t& b, bounds_t * full, const sdf_t& sdf){
        float d = sdf.distance(b.get_centre());
        
        if (d < 0){ 
            full->encapsulate_sphere(b.get_centre(), -d); 
        }
 
        if (std::abs(d) < (b.get_size() / 2.0f).length()){
            for (int i = 0; i < 8; i++){
                helper(b.get_octant(i), full, sdf);
            }
        }
    };


    bounds_t result;
    helper(bounds_t::max_bounds(), &result, *this);
    return result;
}
