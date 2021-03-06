#ifndef SERAPHIM_MATTER_H
#define SERAPHIM_MATTER_H

#include "material.h"

#include "maths/sdf/sdf.h"
#include "maths/quat.h"
#include "physics/constraint.h"
#include "physics/sphere.h"
#include "physics/transform.h"

typedef struct srph_matter {
    srph_transform transform;

    srph_material material;
    srph_sdf * sdf;

    srph_array _vertices;

    bool is_uniform;

    bool _is_mass_calculated;
    double average_density;
    srph::vec3_t centre_of_mass;    

    bool _is_inertia_tensor_valid;
    srph::mat3_t i;

    bool _is_inv_inertia_tensor_valid;
    srph::mat3_t inv_tf_i;

    srph::vec3_t a;
    srph::vec3_t v;
    srph::vec3_t omega;

    srph_material get_material(const vec3 * x);
    srph_sdf * get_sdf() const;
    srph::vec3_t get_position() const;

    srph_bound3 get_moving_bound(double t) const;

    void translate(const srph::vec3_t & x);
    void rotate(const srph::quat_t & q);

    srph::quat_t get_rotation() const;

    bool is_inert();
    
    srph::vec3_t to_local_space(const srph::vec3_t & x) const;

    void physics_tick(double delta);
    
    srph::vec3_t get_velocity(const srph::vec3_t & x);

    double get_inverse_angular_mass(const srph::vec3_t & x, const srph::vec3_t & n);
    
    void apply_impulse_at(const srph::vec3_t & j, const srph::vec3_t & x);

    srph::f32mat4_t get_matrix();

    void reset_acceleration();

    void calculate_centre_of_mass();
    double get_average_density();
    srph::vec3_t get_centre_of_mass();

    srph::mat3_t * get_i();
    srph::mat3_t * get_inv_tf_i();
} srph_matter;

void srph_matter_init(
    srph_matter * m, srph_sdf * sdf, const srph_material * mat, const vec3 * x, bool is_uniform
);
void srph_matter_destroy(srph_matter * m);

double srph_matter_mass(srph_matter * m);
void srph_matter_bound(const srph_matter * m, srph_bound3 * b);
void srph_matter_sphere_bound(const srph_matter * m, double t, srph_sphere * s);


#endif
