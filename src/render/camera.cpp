#include "render/camera.h"

#include "maths/vector.h"

using namespace srph;

camera_t::camera_t(){
    transform.set_position(vec3_t(0.0, 0.5, -5.0));
}

void camera_t::update(double delta, const keyboard_t & keyboard, const mouse_t & mouse){
    vec3_t forward = transform.forward();
    forward[1] = 0.0;
    
    vec3 f1 = { forward[0], forward[1], forward[2] };
    srph_vec3_normalise(&f1, &f1);
    forward = vec3_t(f1.x, f1.y, f1.z);

    vec3_t right = transform.right();

    if (keyboard.is_key_pressed(GLFW_KEY_W)){
        transform.translate(forward * delta);
    }

    if (keyboard.is_key_pressed(GLFW_KEY_S)){
        transform.translate(forward * -delta );
    } 

    if (keyboard.is_key_pressed(GLFW_KEY_A)){
        transform.translate(right * -delta );
    }

    if (keyboard.is_key_pressed(GLFW_KEY_D)){
        transform.translate(right * delta );
    }
    
    transform.rotate(quat_t::angle_axis(
        delta * mouse.get_velocity()[0] / 2000, 
        vec3_t(srph_vec3_up.x, srph_vec3_up.y, srph_vec3_up.z)
    ));

    transform.rotate(quat_t::angle_axis(
        delta * mouse.get_velocity()[1] / 2000, 
        transform.right()
    ));
}

f32mat4_t camera_t::get_matrix(){
    return transform.get_matrix();
}

vec3_t camera_t::get_position() const {
    return transform.get_position();
}
