#include "physics/physics.h"

#include "core/scheduler.h"
#include "physics/collision.h"

#include <chrono>
#include <functional>
#include <iostream>

using namespace srph;

physics_t::physics_t(){
    quit = false;
    thread = std::thread(&physics_t::run, this);
}

physics_t::~physics_t(){
    quit = true;
    thread.join();
}

void physics_t::run(){
    auto t = scheduler::clock_t::now();
    auto clock_d = std::chrono::duration_cast<scheduler::clock_t::duration>(constant::iota);
    auto previous = std::chrono::steady_clock::now() - clock_d;
       
    while (!quit){
        //std::lock_guard<std::mutex> lock(matters_mutex);
        frames++;
        
        auto now = std::chrono::steady_clock::now();
        double delta = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count() / 1000000.0;

        previous = now;

        std::vector<collision_t> collisions;
        
        {
            std::lock_guard<std::mutex> lock(matters_mutex);

            for (auto & m : matters){
                if (m->get_position()[1] > -90.0){
                    m->reset_acceleration();
                }
            }

            for (uint32_t i = 0; i < matters.size(); i++){
                for (uint32_t j = i + 1; j < matters.size(); j++){
                    collisions.emplace_back(delta, matters[i], matters[j]);
                }
            }
            
            for (auto awake_matter : matters){
                for (auto asleep_matter : asleep_matters){
                    collisions.emplace_back(delta, asleep_matter, awake_matter);
                }
            }
        }
        
        for (auto & c : collisions){
            if (c.is_intersecting()){
                c.correct();
            } else if (c.is_anticipated()){
                delta = std::min(delta, c.get_estimated_time());
            }
        }
       
        {
            std::lock_guard<std::mutex> lock(matters_mutex);
 
            for (auto m : matters){
                m->physics_tick(delta);
            } 

            for (uint32_t i = 0; i < matters.size();){
                auto m = matters[i]; 
            
                if (m->is_inert()){
                    asleep_matters.push_back(m);
                    matters[i] = matters[matters.size() - 1];
                    matters.pop_back();
                } else { 
                    i++;
                }
            }
        }

        t += std::chrono::microseconds(static_cast<int64_t>(delta * 1000000.0));
        std::this_thread::sleep_until(t);
    }
}

void physics_t::register_matter(std::shared_ptr<matter_t> matter){
    std::lock_guard<std::mutex> lock(matters_mutex);
    matters.push_back(matter);
}
    
void physics_t::unregister_matter(std::shared_ptr<matter_t> matter){
    std::lock_guard<std::mutex> lock(matters_mutex);
    
    auto it = std::find(matters.begin(), matters.end(), matter);
    if (it != matters.end()){
        matters.erase(it);
    } else {
        it = std::find(asleep_matters.begin(), asleep_matters.end(), matter);
        if (it != asleep_matters.end()){
            asleep_matters.erase(it);
        }
    }
}

int physics_t::get_frame_count(){
    int f = frames;
    frames = 0;
    return f;
}
