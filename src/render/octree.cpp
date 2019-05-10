#include "render/octree.h"

octree_t::octree_t(double render_distance, std::weak_ptr<renderable_t> renderable){
    universal_aabb = aabb_t(vec3_t(-render_distance), render_distance * 2);
    paint(0, universal_aabb, renderable);
}

void
octree_t::request(const vec3_t & x, const vec3_t & camera){
    if (!universal_aabb.contains(x)){
        return;
    }

    aabb_t aabb = universal_aabb;
    uint32_t i = lookup(x, 0, aabb);

    // node already has adequate data
    if (structure[i] != null_node){
        return;
    }

    // remove invisible renderables
    std::vector<std::weak_ptr<renderable_t>> renderables;
    for (auto & renderable_ptr : universal_renderables){
        if (auto renderable = renderable_ptr.lock()){
            if (renderable->is_visible()){
                renderables.push_back(renderable_ptr);
            }
        }
    }
    
    uint32_t start = structure.size();
    subdivide(i, x, camera, aabb, renderables);
    uint32_t end = structure.size();

    // perform copy to GPU
    
}

uint32_t
octree_t::lookup(const vec3_t & x, uint32_t i, aabb_t & aabb) const {
    // base cases
    if (structure[i] == null_node || (structure[i] & is_leaf_flag)) {
        return i;
    } 
    
    // refine octant
    int octant = aabb.get_octant(x);
    aabb.refine(octant);
   
    // tail recurse
    return lookup(x, structure[i] + octant, aabb);
}

void 
octree_t::subdivide(
    uint32_t i,
    const vec3_t & x, const vec3_t & camera, 
    aabb_t & aabb,
    std::vector<std::weak_ptr<renderable_t>> & renderables
){
    std::remove_if(renderables.begin(), renderables.end(), [&aabb](const std::weak_ptr<renderable_t> & renderable_ptr){
        if (auto renderable = renderable_ptr.lock()){
            if (renderable->intersects(aabb)){
                return false;
            }
        }
        return true;
    });

    // check for homogenous volume
    bool is_homogenous = std::any_of(renderables.begin(), renderables.end(), [&aabb](const std::weak_ptr<renderable_t> & renderable_ptr){
        if (auto renderable = renderable_ptr.lock()){
            if (renderable->contains(aabb)){
                return true;
            }
        }
        return false;
    });

    if (renderables.empty() || is_homogenous){
        structure[i] = is_leaf_flag | is_homogenous_flag;

        if (is_homogenous){
            structure[i] |= 1;
        }

        return;
    }

    if (is_leaf(x, camera, aabb, renderables)){
        // TODO
        structure[i] = is_leaf_flag | is_homogenous_flag;
        return;
    }

    // create pointer to final element in structure
    structure[i] = structure.size();

    // create children
    structure.resize(structure.size() + 8, null_node);

    int octant = aabb.get_octant(x);
    aabb.refine(octant);

    // tail recurse
    subdivide(structure[i] + octant, x, camera, aabb, renderables); 
}

bool 
octree_t::is_leaf(
    const vec3_t & x, const vec3_t & camera, const aabb_t & aabb, 
    const std::vector<std::weak_ptr<renderable_t>> & renderables
) const {
    // TODO
    return aabb.get_size() <= 0.1;
}

void 
octree_t::paint(uint32_t i, aabb_t & aabb, std::weak_ptr<renderable_t> renderable_ptr){
    bool is_empty = true;
    bool is_leaf = aabb.get_size() <= 0.1;
    if (auto renderable = renderable_ptr.lock()){
        if (renderable->intersects(aabb)){
            is_empty = false;
        }

        if (renderable->contains(aabb)){
            is_leaf = true;
        }
    }

    if (is_empty || is_leaf){
        structure[i] = is_leaf_flag | is_homogenous_flag;

        if (!is_empty){
            structure[i] |= 1;
        }

        return;
    }

    for (int octant = 0; octant < 8; octant++){
        structure.push_back(null_node);
        aabb_t new_aabb = aabb;
        new_aabb.refine(octant);
        paint(structure[i] + octant, new_aabb, renderable_ptr);
    }
}