#include "substance/substance.h"

#include <iostream>

substance_t::data_t::data_t(){
    id = ~0;
}

substance_t::data_t::data_t(const f32vec3_t & r, uint32_t id, const f32mat4_t & transform){
    this->r = r;
    this->id = id;
    this->transform = transform;
}

substance_t::substance_t(std::shared_ptr<form_t> form, std::shared_ptr<matter_t> matter){
    static uint32_t id = 0;

    this->form = form;
    this->id = id++;
    this->matter = matter;
}

std::shared_ptr<form_t>
substance_t::get_form() const {
    return form;
}

substance_t::data_t
substance_t::get_data(){
    return data_t(
        form->get_aabb()->get_size().cast<float>(),
        id,
        *transform.get_matrix()
    );
}

uint32_t 
substance_t::get_id() const {
    return id;
}

vec3_t 
substance_t::get_position() const {
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
    return form->get_sdf()->phi(transform.to_local_space(x));
}

std::shared_ptr<matter_t> 
substance_t::get_matter() const {
    return matter;
}
