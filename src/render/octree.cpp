#include "render/octree.h"

#include <iostream>
#include "core/blaspheme.h"
#include <set>
#include "sdf/compose.h"
#include "sdf/mutate.h"
#include "maths/mat.h"

constexpr uint32_t octree_t::null_node;

octree_t::octree_t(
    const allocator_t & allocator, 
    const std::vector<std::weak_ptr<sdf3_t>> & sdfs, 
    const std::vector<VkDescriptorSet> & desc_sets
){
    this->sdfs = sdfs;
    structure.reserve(max_structure_size);

    universal_aabb = vec4_t(-hyper::rho);
    universal_aabb[3] = hyper::rho * 2;

    texture_manager = std::make_shared<texture_manager_t>(allocator, desc_sets);
    
    // create buffers
    uint32_t octree_size = sizeof(uint32_t) * max_structure_size;
    octree_buffer = std::make_unique<buffer_t>(
        allocator, octree_size,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    uint32_t request_size = sizeof(request_t) * max_requests_size;
    request_buffer = std::make_unique<buffer_t>(
        allocator, request_size,
        VMA_MEMORY_USAGE_GPU_TO_CPU
    );

    // write to descriptor sets
    std::vector<VkDescriptorBufferInfo> desc_buffer_infos = {
        octree_buffer->get_descriptor_info(),
        request_buffer->get_descriptor_info()
    };

    VkWriteDescriptorSet write_desc_set = {};
    write_desc_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_desc_set.pNext = nullptr;
    write_desc_set.dstArrayElement = 0;
    write_desc_set.descriptorCount = 1;
    write_desc_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_desc_set.pImageInfo = nullptr;
    write_desc_set.pTexelBufferView = nullptr;

    std::vector<VkWriteDescriptorSet> write_desc_sets;
    for (uint32_t i = 0; i < desc_sets.size(); i++){
        write_desc_set.dstSet = desc_sets[i];

        for (uint32_t j = 0; j < desc_buffer_infos.size(); j++){
            write_desc_set.dstBinding = j + 1;
            write_desc_set.pBufferInfo = &desc_buffer_infos[j];
            write_desc_sets.push_back(write_desc_set);
        }
    }

    vkUpdateDescriptorSets(allocator.device, write_desc_sets.size(), write_desc_sets.data(), 0, nullptr);

    structure = { create_node(universal_aabb, 0) };
    octree_buffer->copy(structure.data(), sizeof(uint32_t), 0);
}

std::tuple<bool, bool> 
octree_t::intersects_contains(const vec4_t & aabb, std::shared_ptr<sdf3_t> sdf) const {
    double upper_radius = vec3_t(aabb[3] / 2).norm();
    vec3_t c = vec3_t(aabb[0], aabb[1], aabb[2]) + vec3_t(aabb[3] / 2.0f);
    double p = sdf->phi(c);

    // containment check upper bound
    if (p <= -upper_radius){
        return std::make_tuple(false, true);
    }

    // intersection check lower bound
    if (std::abs(p) <= aabb[3] / 2){
        return std::make_tuple(true, false);
    }

    // intersection check upper bound
    if (p >= upper_radius){
        return std::make_tuple(false, false);
    }

    // containment check precise
    double d = (sdf->normal(c) * p).chebyshev_norm();
    if (p < 0 && d > aabb[3] / 2){
        return std::make_tuple(false, true);
    }

    // intersection check precise
    if (d <= aabb[3] / 2){
        return std::make_tuple(true, false);
    }

    return std::make_tuple(false, false);
}

uint32_t 
octree_t::lookup(const f32vec3_t & x, uint32_t i, vec4_t & aabb) const {
    if ((structure[i] & is_leaf_flag) != 0){
        return i;
    }

    i = structure[i];

    aabb[3] /= 2;

    for (uint8_t a = 0; a < 3; a++){
        if (x[a] > aabb[a] + aabb[3]){
            aabb[a] += aabb[3];
            i += 1 << a;
        }
    }

    return lookup(x, i, aabb);    
}


uint32_t 
octree_t::create_node(const vec4_t & aabb, uint32_t index){
    std::vector<std::shared_ptr<sdf3_t>> new_sdfs;

    for (auto sdf_ptr : sdfs){
        if (auto sdf = sdf_ptr.lock()){
            auto intersection = intersects_contains(aabb, sdf);
            // contains
            if (std::get<1>(intersection)){
                return is_leaf_flag;
            }

            // intersects
            if (std::get<0>(intersection)){
                new_sdfs.push_back(sdf);
            }
        }
    }

    if (new_sdfs.empty()){
        return is_leaf_flag;
    }

    compose::union_t<3> sdf(new_sdfs);

    vec3_t c = vec3_t(aabb[0], aabb[1], aabb[2]) + vec3_t(aabb[3] / 2);
    vec3_t n = sdf.normal(c);
    u8vec4_t colour = painter_t<3>().colour(c - n * sdf.phi(c));
    
    n = (n / 2 + 0.5) * 255;
    u8vec4_t normal(n[0], n[1], n[2], 0);
    uint32_t id = texture_manager->request(colour, normal);
    
    return is_leaf_flag | id; 
}

void 
octree_t::handle_request(const f32vec3_t & x){
    vec4_t aabb = universal_aabb;
    uint32_t node_index = lookup(x, 0, aabb);
    
    uint32_t brick_id = (structure[node_index] & brick_id_mask);
    if (brick_id > 0){
        texture_manager->clear(brick_id);

        structure[node_index] = structure.size();

        for (uint8_t octant = 0; octant < 8; octant++){
            vec4_t new_aabb = aabb;
            new_aabb[3] /= 2;
            if (octant & 1) new_aabb[0] += new_aabb[3];
            if (octant & 2) new_aabb[1] += new_aabb[3];
            if (octant & 4) new_aabb[2] += new_aabb[3];
            structure.push_back(create_node(new_aabb, node_index));
        }
    }
}

void
octree_t::handle_requests(){
    request_t data[max_requests_size];
    request_buffer->read(&data, sizeof(request_t) * max_requests_size);

    bool changed = false;

    for (uint32_t i = 0; i < max_requests_size; i++){
        if (data[i].i != 0){
            changed = true;
            handle_request(data[i].x);
        }
    }    
    
    if (changed){
        // std::cout << "octree size: " << structure.size() << std::endl;
        // std::cout << "brickset size: " << brickset.size() << std::endl;
        octree_buffer->copy(structure.data(), sizeof(uint32_t) * max_structure_size, 0);
    }

    static request_t clear_requests[max_requests_size];
    request_buffer->copy(&clear_requests, sizeof(request_t) * max_requests_size, 0);
}