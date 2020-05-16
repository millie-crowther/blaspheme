#include "render/substance.h"

#include <iostream>

substance_t::data_t::data_t(){
    id = ~0;
}

substance_t::data_t::data_t(const f32vec3_t & c, int32_t root, float r, uint32_t rotation, uint32_t id){
    this->c = c;
    this->root = root;
    this->r = r;
    this->rotation = rotation;
    this->id = id;
}

substance_t::substance_t(uint32_t id, int32_t root, std::shared_ptr<sdf3_t> sdf, std::shared_ptr<matter_t> matter){
    this->sdf = sdf;
    this->root = root;
    this->id = id;
    this->matter = matter;
}

std::weak_ptr<sdf3_t>
substance_t::get_sdf() const {
    return sdf;
}

substance_t::data_t
substance_t::get_data(){
    return data_t(
        get_aabb().lock()->get_centre().cast<float>(),
        root,

        static_cast<float>(get_aabb().lock()->get_size().chebyshev_norm()),
        transform.get_rotation().inverse().pack(),
        id
    );
}

void
substance_t::create_aabb(){
    aabb = std::make_shared<aabb3_t>();
    aabb->capture_sphere(sdf->normal(vec3_t()) * -sdf->phi(vec3_t()), hyper::epsilon);

    bool has_touched_surface = true;

    const double precision = 32.0;

    for (uint32_t i = 0; i < 32 && has_touched_surface; i++){
        has_touched_surface = false;

        for (uint32_t face = 0; face < 6; face++){
            uint8_t ui = face % 3;
            uint8_t vi = (face + 1) % 3;
            uint8_t wi = (face + 2) % 3;

            vec3_t min = aabb->get_min() - hyper::epsilon;
            vec3_t max = aabb->get_max() + hyper::epsilon;

            vec3_t x;
            x[wi] = face < 3 ? min[wi] : max[wi];

            double du = std::max((max[ui] - min[ui]) / precision, hyper::epsilon);

            for (x[ui] = min[ui]; x[ui] < max[ui]; x[ui] += du){
                for (x[vi] = min[vi]; x[vi] < max[vi]; x[vi] += hyper::epsilon){
                    auto phi = sdf->phi(x);

                    if (phi < 0){
                        has_touched_surface = true;
                        aabb->capture_sphere(x, phi);
                    }

                    x[vi] += std::abs(phi);
                }
            }
        }   
    }
}

uint32_t 
substance_t::get_id() const {
    return id;
}

std::weak_ptr<aabb3_t> 
substance_t::get_aabb(){
    if (aabb == nullptr){
        create_aabb();
    }

    return aabb;
}

vec3_t substance_t::get_position() const {
    return transform.get_position();
}

void 
substance_t::set_position(const vec3_t & x){
    transform.set_position(x);
}

void 
substance_t::set_rotation(const quat_t & q){
    transform.set_rotation(q);
}

double 
substance_t::phi(const vec3_t & x) const {
    return sdf->phi(transform.to_local_space(x));
}

std::weak_ptr<matter_t> 
substance_t::get_matter() const {
    return matter;
}
