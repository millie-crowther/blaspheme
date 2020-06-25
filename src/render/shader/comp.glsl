#version 450

layout (local_size_x = 32, local_size_y = 32) in;

layout (binding = 10) uniform writeonly image2D render_texture;
layout (binding = 11) uniform sampler3D normal_texture;
layout (binding = 12) uniform sampler3D colour_texture;

// constants
const uint node_empty_flag = 1 << 24;
const uint node_unused_flag = 1 << 25;
const uint node_child_mask = 0xFFFF;
const int octree_pool_size = int(gl_WorkGroupSize.x * gl_WorkGroupSize.y);

const float sqrt3 = 1.73205080757;
const int max_steps = 64;
const float epsilon = 1.0 / 256.0;

const ivec3 p = ivec3(
    6291469,
    12582917,
    25165843
);

layout( push_constant ) uniform push_constants {
    uvec2 window_size;
    float render_distance;
    uint current_frame;

    vec3 camera_position;
    float phi_initial;        

    vec3 eye_right;
    float focal_depth;

    vec3 eye_up;
    float dummy4;
} pc;

struct ray_t {
    vec3 x;
    vec3 d;
};

struct substance_t {
    vec3 c;
    int _1;

    vec3 r;
    uint id;

    mat4 transform;
};

struct node_t {
    uint index;
    uint hash;
    vec3 centre;
    float size;
    bool is_valid;
    uint data;
};

struct intersection_t {
    bool hit;
    vec3 x;
    vec3 normal;
    float distance;
    substance_t substance;
    vec3 local_x;
    node_t node;
};

struct request_t {
    vec3 c;
    float size;

    uint index;
    uint hash;
    uint substanceID;
    uint status;
};

struct light_t {
    vec3 x;
    uint id;

    vec4 colour;
};

layout (binding = 1) buffer octree_buffer    { uint        data[]; } octree_global;
layout (binding = 2) buffer request_buffer   { request_t   data[]; } requests;
layout (binding = 3) buffer lights_buffer    { light_t     data[]; } lights_global;
layout (binding = 4) buffer substance_buffer { substance_t data[]; } substance;

// shared memory
shared substance_t substances[gl_WorkGroupSize.x];
shared uint substances_visible;

shared substance_t shadows[gl_WorkGroupSize.x];
shared uint shadows_visible;

shared light_t lights[gl_WorkGroupSize.x];
shared uint lights_visible;

shared uint octree[octree_pool_size];

shared bool hitmap[octree_pool_size];
shared vec4 workspace[gl_WorkGroupSize.x * gl_WorkGroupSize.y];

vec2 uv(vec2 xy){
    vec2 uv = xy / (gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);
    uv = uv * 2.0 - 1.0;
    uv.y *= -float(gl_NumWorkGroups.y) / gl_NumWorkGroups.x;
    return uv;
}

uint expected_order(vec3 x){
    return uint(
        2
        // length((x - pc.camera_position) / 10) +W
        // length(uv(gl_GlobalInvocationID.xy))
    );
}

float expected_size(vec3 x){
    return 0.075 * (1 << expected_order(x));
}

node_t hash_octree(vec3 x, vec3 local_x, uint substance_id){
    // find the expected size and order of magnitude of cell
    uint order = expected_order(x); 
    float size = expected_size(x);

    // snap to grid, making sure not to duplicate zero
    ivec3 x_grid = ivec3(floor(local_x / size));

    // do a shitty hash to all the relevant fields
    uvec2 os_hash = (ivec2(order, substance_id) * p.x + p.y) % p.z;
    uvec3 x_hash  = (x_grid * p.y + p.z) % p.x;

    // maybe do sum instead of XOR of x_hash elements
    // - more location-aware hash
    uint full_hash = os_hash.x ^ os_hash.y ^ x_hash.x ^ x_hash.y ^ x_hash.z;
    uint hash = (full_hash >> 16) ^ (full_hash & 0xFFFF);

    // calculate some useful variables for doing lookups
    uint index = full_hash % octree_pool_size;
    vec3 centre = x_grid * size + size / 2;
    bool is_valid = (octree[index] & 0xFFFF) == hash;

    return node_t(index, hash, centre, size, is_valid, octree[index]);
}

uint work_group_offset(){
    return (gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x) * octree_pool_size;
}

bool is_leaf(uint i){
    return (octree[i] & node_child_mask) >= octree_pool_size;
}

float phi_s(vec3 global_x, substance_t sub, float expected_size, inout intersection_t intersection, inout request_t request){
    vec3 x = (sub.transform * vec4(global_x, 1)).xyz;

    // check against outside bounds of aabb
    bool outside_aabb = any(greaterThan(abs(x), vec3(sub.r + epsilon)));
    float phi_aabb = length(max(abs(x) - sub.r, 0)) + epsilon;

    node_t node = hash_octree(global_x, x, sub.id);

    // if necessary, request more data from CPU
    intersection.local_x = x.xyz;
    intersection.node = node;
    intersection.substance = sub;

    if (node.is_valid){
        hitmap[node.index] = true;
    } else if (!outside_aabb) {
        request = request_t(node.centre, node.size, node.index + work_group_offset(), node.hash, sub.id, 1);
    }

    float vs[8];
    for (int o = 0; o < 8; o++){
        vs[o] = mix(-node.size, node.size, (node.data & (1 << (o + 16))) != 0);
    }

    vec3 alpha = (x.xyz - node.centre + node.size) / (node.size * 2);

    vec4 x1 = mix(vec4(vs[0], vs[1], vs[2], vs[3]), vec4(vs[4], vs[5], vs[6], vs[7]), alpha.z);
    vec2 x2 = mix(x1.xy, x1.zw, alpha.y);
    float phi_plane = mix(x2.x, x2.y, alpha.x);
    phi_plane = mix(node.size, phi_plane, node.is_valid);

    return mix(phi_plane, phi_aabb, outside_aabb);
}

intersection_t raycast(ray_t r, inout request_t request){
    uint steps;
    intersection_t i;
    
    i.hit = false;
    i.distance = 0;

    for (steps = 0; !i.hit && steps < max_steps; steps++){
        float expected_size = expected_size(r.x);
        float phi = pc.render_distance;
        for (uint substanceID = 0; !i.hit && substanceID < substances_visible; substanceID++){
            phi = min(phi, phi_s(r.x, substances[substanceID], expected_size, i, request));
            i.hit = i.hit || phi < epsilon;
        }
        r.x += r.d * phi;
        i.distance += phi;
    }
    
    i.x = r.x;
    return i;
}

float shadow(vec3 l, intersection_t i, inout request_t request){
    bool hit = false;
    uint steps;

    intersection_t _;
    uint hit_sub_id = 0;

    ray_t r = ray_t(l, normalize(i.x - l));
    
    for (steps = 0; !hit && steps < max_steps; steps++){
        float expected_size = expected_size(r.x);
        float phi = pc.render_distance;
        for (uint substanceID = 0; !hit && substanceID < shadows_visible; substanceID++){
            substance_t sub = shadows[substanceID];
            hit_sub_id = sub.id;
            phi = min(phi, phi_s(r.x, sub, expected_size, _, request));
            hit = hit || phi < epsilon;
        }
        r.x += r.d * phi;
    }
    
    bool shadow = hit_sub_id != i.substance.id;
    return float(!shadow);
}

vec4 light(light_t light, intersection_t i, vec3 n, inout request_t request){
    const float shininess = 16;

    // attenuation
    vec3 dist = light.x - i.x;
    float attenuation = 1.0 / dot(dist, dist);

    //shadows
    float shadow = shadow(light.x, i, request);

    //diffuse
    vec3 l = normalize(light.x - i.x);
    float d = 0.75 * max(epsilon, dot(l, n));

    //specular
    vec3 v = normalize(-i.x);

    vec3 h = normalize(l + v);
    float s = 0.4 * pow(max(dot(h, n), 0.0), shininess);

    return (d + s) * attenuation * shadow * light.colour;
}

uvec4 reduce_to_fit(uint i, bvec4 hits, out uvec4 totals, uvec4 limits){
    barrier();
    workspace[i] = uvec4(hits);
    barrier();

    if ((i &   1) != 0) workspace[i] += workspace[i &   ~1      ];    
    barrier();
    if ((i &   2) != 0) workspace[i] += workspace[i &   ~2 |   1];    
    barrier();
    if ((i &   4) != 0) workspace[i] += workspace[i &   ~4 |   3];    
    barrier();
    if ((i &   8) != 0) workspace[i] += workspace[i &   ~8 |   7];    
    barrier();
    if ((i &  16) != 0) workspace[i] += workspace[i &  ~16 |  15];    
    barrier();
    if ((i &  32) != 0) workspace[i] += workspace[i &  ~32 |  31];    
    barrier();
    if ((i &  64) != 0) workspace[i] += workspace[i &  ~64 |  63];    
    barrier();
    if ((i & 128) != 0) workspace[i] += workspace[i & ~128 | 127];    
    barrier();
    if ((i & 256) != 0) workspace[i] += workspace[i & ~256 | 255];    
    barrier();
    if ((i & 512) != 0) workspace[i] += workspace[           511];    
    barrier();

    totals = min(uvec4(workspace[1023]), limits);
    barrier();

    bvec4 mask = lessThanEqual(workspace[i], limits) && hits;
    barrier();

    uvec4 result = uvec4(workspace[i]);
    barrier();

    return mix(uvec4(~0), result - 1, mask);
}

vec4 reduce_min(uint i, vec4 value){
    barrier();
    workspace[i] = value;
    barrier();
    if ((i & 0x001) == 0) workspace[i] = min(workspace[i], workspace[i +   1]);
    if ((i & 0x003) == 0) workspace[i] = min(workspace[i], workspace[i +   2]);
    if ((i & 0x007) == 0) workspace[i] = min(workspace[i], workspace[i +   4]);
    if ((i & 0x00F) == 0) workspace[i] = min(workspace[i], workspace[i +   8]);
    if ((i & 0x01F) == 0) workspace[i] = min(workspace[i], workspace[i +  16]);
    if ((i & 0x03F) == 0) workspace[i] = min(workspace[i], workspace[i +  32]);
    if ((i & 0x07F) == 0) workspace[i] = min(workspace[i], workspace[i +  64]);
    if ((i & 0x0FF) == 0) workspace[i] = min(workspace[i], workspace[i + 128]);
    if ((i & 0x1FF) == 0) workspace[i] = min(workspace[i], workspace[i + 256]);
    if ((i & 0x3FF) == 0) workspace[i] = min(workspace[i], workspace[i + 512]);
    
    return workspace[0];
}

float phi_s_initial(vec3 d, vec3 centre, float r){
    float a = dot(d, d);
    float b = -2.0 * dot(centre, d);
    float c = dot(centre, centre) - 3 * r * r;
    float discriminant = b * b - 4 * a * c;
    float dist = (-b - sqrt(discriminant)) / (2.0 * a);
    float result = mix(dist, pc.render_distance, discriminant < 0);
    return max(0, result);
}

request_t render(uint i, vec3 d, float phi_initial){
    request_t request;
    request.status = 0;

    ray_t r = ray_t(pc.camera_position + d * phi_initial, d);
    intersection_t intersection = raycast(r, request);

    const vec4 sky = vec4(0.5, 0.7, 0.9, 1.0);
   
    vec3 t = intersection.local_x - intersection.node.centre;
    t += intersection.node.size * 2;
    t /= intersection.node.size * 4;
    t.xy += vec2(
        (intersection.node.index + work_group_offset()) % octree_pool_size,
        (intersection.node.index + work_group_offset()) / octree_pool_size
    );
    t.xy /= vec2(octree_pool_size, gl_NumWorkGroups.x * gl_NumWorkGroups.y);
    
    vec3 n = (
        inverse(intersection.substance.transform) * normalize(vec4(texture(normal_texture, t).xyz - 0.5, 0))
    ).xyz;

    // ambient
    vec4 l = vec4(0.25, 0.25, 0.25, 1.0);

    for (uint i = 0; i < lights_visible; i++){
        l += light(lights[i], intersection, n, request);
    }

    vec4 hit_colour = vec4(texture(colour_texture, t).xyz, 1.0) * l;
    imageStore(render_texture, ivec2(gl_GlobalInvocationID.xy), mix(sky, hit_colour, intersection.hit));

    return request;
}

vec2 project(vec3 x){
    float d = dot(x, cross(pc.eye_right, pc.eye_up));
    vec2 t = vec2(dot(x, pc.eye_right), dot(x, pc.eye_up)) / d * pc.focal_depth;
    t.y *= -float(gl_NumWorkGroups.x) / gl_NumWorkGroups.y;

    return (t + 1) * gl_NumWorkGroups.xy * gl_WorkGroupSize.xy / 2;
}

bool is_shadow_visible(uint i, vec3 x){
    // vec2 p_x = project(x);

    // vec4 bounds = reduce_min(i, vec4(p_x, -p_x));

    // vec2 s_min = bounds.xy;
    // vec2 s_max = -bounds.zw;

    return true;
}

bool is_sphere_visible(vec3 centre, float radius){
    vec3 x = centre - pc.camera_position;
    vec2 image_x = project(x);
    float d = max(epsilon, dot(x, cross(pc.eye_right, pc.eye_up)));
    float r = radius / d * pc.focal_depth * gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    vec2 c = gl_WorkGroupID.xy * gl_WorkGroupSize.xy + gl_WorkGroupSize.xy / 2;
    vec2 diff = max(ivec2(0), abs(c - image_x) - ivec2(gl_WorkGroupSize.xy / 2));

    return length(diff) < r;
}

vec3 get_ray_direction(uvec2 xy){
    vec2 uv = uv(xy);
    vec3 up = pc.eye_up;
    vec3 right = pc.eye_right;
    vec3 forward = cross(right, up);
    return normalize(forward * pc.focal_depth + right * uv.x + up * uv.y);
}

float prerender(uint i, uint work_group_id, vec3 d){
    // clear shared variables
    hitmap[i] = false;

    // load shit
    substance_t s = substance.data[i];
    bool directly_visible = s.id != ~0 && is_sphere_visible(s.c, length(s.r));

    light_t l = lights_global.data[i];
    bool light_visible = l.id != ~0;// && is_sphere_visible(l.x, sqrt(length(l.colour) / epsilon));

    // load octree from global memory into shared memory
    octree[i] = octree_global.data[i  + work_group_offset()];
   
    // visibility check on substances and load into shared memory
    barrier();
    bvec4 hits = bvec4(directly_visible, light_visible, false, false);
    uvec4 totals;
    uvec4 limits = uvec4(gl_WorkGroupSize.xx, 0, 0);
    uvec4 indices = reduce_to_fit(i, hits, totals, limits);

    substances_visible = totals.x;
    if (indices.x != ~0){
        substances[indices.x] = s;
    }

    lights_visible = totals.y;
    if (indices.y != ~0){
        lights[indices.y] = l;
    }

    barrier();
    bool shadow_visible = s.id != ~0 && is_shadow_visible(i, vec3(0));
    barrier();
    hits = bvec4(shadow_visible, false, false, false);
    indices = reduce_to_fit(i, hits, totals, uvec4(gl_WorkGroupSize.x));
    shadows_visible = totals.x;
    if (indices.x != ~0){
        shadows[indices.x] = s;
    }

    // calculate initial distance
    // float value = mix(pc.render_distance, phi_s_initial(d, s.c, s.r), s.id != ~0 && directly_visible);
    // float phi_initial = reduce_min(i, vec4(value)).x;

    // return phi_initial;
    return 0;
}

void postrender(uint i, request_t request){
    bvec4 hits = bvec4(request.status != 0 && !hitmap[request.index], false, false, false);
    uvec4 _;
    uvec4 limits = uvec4(1, 0, 0, 0);
    uvec4 indices = reduce_to_fit(i, hits, _, limits);
    if (indices.x != ~0){
        requests.data[(gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x) * 4 + indices.x] = request;
    }
}

void main(){
    uint work_group_id = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint i = gl_LocalInvocationID.x + gl_LocalInvocationID.y * gl_WorkGroupSize.x;

    vec3 d = get_ray_direction(gl_GlobalInvocationID.xy);

    float phi_initial = prerender(i, work_group_id, d);

    barrier();
    request_t request = render(i, d, phi_initial);

    barrier();

    postrender(i, request);
}
