#ifndef SUBSTANCE_H
#define SUBSTANCE_H

#include <memory>

#include "scene/transform.h"
#include "form/form.h"
#include "matter/matter.h"

class substance_t {
public:
    struct data_t {
        f32vec3_t c;
        int32_t root;

        float r;
        uint32_t rotation;
        uint32_t _2;
        uint32_t id;

        data_t();
        data_t(const f32vec3_t & c, int32_t root, float r, uint32_t rotation, uint32_t id);
    };

    substance_t(uint32_t id, std::shared_ptr<form_t> form, std::shared_ptr<matter_t> matter);

    std::shared_ptr<form_t> get_form() const;
    std::shared_ptr<matter_t> get_matter() const;
    data_t get_data();
    uint32_t get_id() const;

    vec3_t get_position() const;
    void set_position(const vec3_t & x);

    void set_rotation(const quat_t & q);

    double phi(const vec3_t & x) const;

private:
    uint32_t id;
    std::shared_ptr<form_t> form;
    std::shared_ptr<matter_t> matter;

    transform_t transform;

};

#endif