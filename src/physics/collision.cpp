#include "physics/collision.h"

using namespace seraph::physics;

collision_t::collision_t(
    const vec3_t & x, double fx, double t, 
    std::shared_ptr<matter_t> a, std::shared_ptr<matter_t> b
){
    this->hit = fx <= 0.0;
    this->x = x;
    this->fx = fx;
    this->t = t;
    this->a = a;
    this->b = b;
}

bool
collision_t::comparator_t::operator()(const collision_t & a, const collision_t & b){
    if (a.hit != b.hit){
        return a.hit && !b.hit;
    } else {
        return a.t < b.t;
    }
}

collision_t
seraph::physics::collide(std::shared_ptr<matter_t> a, std::shared_ptr<matter_t> b){
    static const int max_iterations = 50;

    // detect collision
    auto f = [a, b](const vec3_t & x, double t){
        vec3_t x_a   = a->get_transform_at(t).to_local_space(x);
        double phi_a = a->get_sdf()->phi(x_a); 
        
        vec3_t x_b   = b->get_transform_at(t).to_local_space(x);
        double phi_b = b->get_sdf()->phi(x_b); 

        return std::max(phi_a, phi_b);
    };

    auto dfdx = [a, b](const vec3_t & x, double t){
        transform_t ta    = a->get_transform_at(t);
        vec3_t      x_a   = ta.to_local_space(x);
        double      phi_a = a->get_sdf()->phi(x_a); 
        
        transform_t tb    = b->get_transform_at(t);
        vec3_t      x_b   = tb.to_local_space(x);
        double      phi_b = b->get_sdf()->phi(x_b); 
        
        if (phi_a > phi_b){
            return ta.get_rotation() * a->get_sdf()->normal(x_a); 
        } else {
            return tb.get_rotation() * b->get_sdf()->normal(x_b); 
        }
    }; 
    
    auto x = (a->get_position() + b->get_position()) / 2.0;
    double t = 0.0;
    auto fx = f(x, t);
    auto dfdx_ = dfdx(x, t);

    for (int i = 0; i < max_iterations && fx > 0; i++){
        x -= dfdx_ * fx;
        fx = f(x, t);
        dfdx_ = dfdx(x, t);
    }

    return collision_t(x, fx, /* TODO */ 0.0, a, b);
}

void
seraph::physics::collision_correct(const collision_t & collision){
    auto fx = collision.fx;
    auto a = collision.a;
    auto b = collision.b;
    auto x = collision.x;
     
    auto x_a = a->get_transform().to_local_space(x);
    auto n = a->get_transform().get_rotation() * a->get_sdf()->normal(x_a);
    
    auto sm = a->get_mass() + b->get_mass();
    double da = fx * b->get_mass() / sm;
    double db = fx * a->get_mass() / sm;
    a->get_transform().translate(-da * n);
    b->get_transform().translate( db * n);     
 
    // calculate collision impulse magnitude
    auto va = a->get_velocity(x);
    auto ra = a->get_offset_from_centre_of_mass(x);
    auto ia = mat::inverse(a->get_inertia_tensor());
    auto xa = vec::cross(ia * vec::cross(ra, n), ra); 
    auto ma = 1.0 / a->get_mass();
    auto mata = a->get_material(a->to_local_space(x));

    auto vb = b->get_velocity(x);
    auto rb = b->get_offset_from_centre_of_mass(x);
    auto ib = mat::inverse(b->get_inertia_tensor());
    auto xb = vec::cross(ib * vec::cross(rb, n), rb);
    auto mb = 1.0 / b->get_mass();
    auto matb = b->get_material(b->to_local_space(x));

    double CoR = std::max(mata.restitution, matb.restitution);
    double mu_s = std::max(mata.static_friction, matb.static_friction);
    double mu_d = std::max(mata.dynamic_friction, matb.dynamic_friction);
    auto vr = vb - va;
 
    double jr = 
        -(1.0 + CoR) * vec::dot(vr, n) /
        (ma + mb + vec::dot(xa + xb, n));

    std::cout << "collision detected!" << std::endl;
    std::cout << "\t f(x) = " << fx << std::endl;
    std::cout << "\t n    = " << n  << std::endl;
    std::cout << "\t |vr| = " << vec::length(vr) << std::endl;
    
    // calculate frictional force
    vec3_t t  = vec::normalise(vr - vec::dot(vr, n) * n);
    double js = mu_s * jr;
    double jd = mu_d * jr;
    double vrt = vec::dot(vr, t);

    double vrta = a->get_mass() * vrt;
    vec3_t jfa = - (vrta <= js ? vrta : jd) * t;
    
    double vrtb = b->get_mass() * vrt;
    vec3_t jfb = - (vrtb <= js ? vrtb : jd) * t;

    jfa = vec3_t();
    jfb = vec3_t();

    // update velocities accordingly
    vec3_t ja  = -jr * n + jfa;
    vec3_t dva = ja / a->get_mass();
    vec3_t dwa = ia * vec::cross(ra, ja); 
    a->update_velocities(dva, dwa);

    vec3_t jb  =  jr * n + jfb;
    vec3_t dvb = jb / b->get_mass();
    vec3_t dwb = ib * vec::cross(rb, jb); 
    b->update_velocities(dvb, dwb);
}