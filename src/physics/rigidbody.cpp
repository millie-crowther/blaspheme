#include "physics/rigidbody.h"

rigidbody_t::rigidbody_t(const std::shared_ptr<collider_t> & collider){
    this->collider = collider;
}

std::shared_ptr<transform_t> 
rigidbody_t::add_child(){
    return transform.add_child();
}

void
rigidbody_t::add_force(const vec3_t & f){
    add_force_at(f, vec3_t());
}

void 
rigidbody_t::add_force_at(const vec3_t & f, const vec3_t & s){
    
}