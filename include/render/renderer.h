#ifndef RENDERER_H
#define RENDERER_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <memory>

#include <chrono>
#include <list>
#include <map>
#include <set>

#include "core/buffer.h"
#include "ui/window.h"
#include "render/camera.h"
#include "render/light.h"
#include "render/swapchain.h"
#include "render/texture.h"
#include "core/command.h"
#include "metaphysics/substance.h"
#include "render/call_and_response.h"

namespace srph {
    class renderer_t {
    private:
        // types
        struct push_constant_t {
            u32vec2_t window_size;
            float render_distance;
            uint32_t current_frame;

            float phi_initial;           
            float focal_depth;
            uint32_t number_of_calls;
            uint32_t _1;

            f32mat4_t eye_transform;

            uint32_t texture_size;
            uint32_t texture_depth;
            uint32_t patch_pool_size;
            float epsilon;
        };

        // constants
        static constexpr uint8_t frames_in_flight = 2;
        static constexpr uint32_t number_of_calls = 2048;
        static constexpr uint32_t number_of_patches = 1000000;
        static constexpr uint32_t patch_sample_size = 2;
        static constexpr uint32_t max_cache_size = 1000;  

        std::set<uint32_t> indices;
        std::set<uint32_t> hashes;

        // fields
        u32vec2_t work_group_count;
        u32vec2_t work_group_size;
        uint32_t patch_image_size; 
        push_constant_t push_constants;
        device_t * device;
        std::vector<VkFramebuffer> framebuffers;
        VkSurfaceKHR surface;
        VkRenderPass render_pass;

        VkPipeline graphics_pipeline;
        VkPipelineLayout pipeline_layout;
        std::vector<std::shared_ptr<command_buffer_t>> command_buffers;

        VkPipeline compute_pipeline;
        VkPipelineLayout compute_pipeline_layout;

        int frames;
        int current_frame;
        std::vector<VkSemaphore> image_available_semas;
        std::vector<VkSemaphore> compute_done_semas;
        std::vector<VkSemaphore> render_finished_semas;
        std::vector<VkFence> in_flight_fences;

        VkDescriptorSetLayout descriptor_layout;
        std::vector<VkDescriptorSet> desc_sets;
        VkDescriptorPool desc_pool;

        VkQueue present_queue;
        
        std::string fragment_shader_code;
        std::string vertex_shader_code;

        std::set<std::shared_ptr<substance_t>, substance_t::comparator_t> substances;

        std::unique_ptr<swapchain_t> swapchain;
        std::weak_ptr<camera_t> main_camera;

        // textures
        std::unique_ptr<texture_t> render_texture; 
        std::unique_ptr<texture_t> colour_texture;
        std::unique_ptr<texture_t> normal_texture;
        
        // command pool
        std::unique_ptr<command_pool_t> compute_command_pool;
        std::unique_ptr<command_pool_t> graphics_command_pool;

        // buffers
        std::unique_ptr<device_buffer_t<response_t::patch_t>> patch_buffer;
        std::unique_ptr<device_buffer_t<substance_t::data_t>> substance_buffer;
        std::unique_ptr<device_buffer_t<call_t>> call_buffer;
        std::unique_ptr<device_buffer_t<light_t>> light_buffer;
        std::unique_ptr<device_buffer_t<uint32_t>> pointer_buffer;
        std::unique_ptr<device_buffer_t<f32vec2_t>> frustum_buffer;
        std::unique_ptr<device_buffer_t<f32vec4_t>> lighting_buffer;
      
        std::map<call_t, response_t, call_t::comparator_t> response_cache;
        std::list<std::map<call_t, response_t, call_t::comparator_t>::iterator> prev_calls;

        std::chrono::high_resolution_clock::time_point start;

        // initialisation functions
        VkShaderModule create_shader_module(std::string code);
        void create_render_pass();
        void create_graphics_pipeline();    
        void create_compute_pipeline();
        void create_framebuffers();
        void create_command_buffers();
        void create_descriptor_set_layout();
        void create_descriptor_pool();
        void create_sync();
        void create_compute_command_buffers();
        void create_buffers();

        // helper functions
        void recreate_swapchain();
        void cleanup_swapchain();
        void handle_requests(uint32_t frame);
        void present(uint32_t image_index) const;
        response_t get_response(const call_t & call, std::weak_ptr<substance_t> substance);   
        
    public:
        // constructors and destructors
        renderer_t(
            device_t * device,
            VkSurfaceKHR surface, window_t * window,
            std::shared_ptr<camera_t> test_camera,
            u32vec2_t work_group_count, u32vec2_t work_group_size,
            uint32_t max_image_size
        );
        ~renderer_t();

        // public functions
        void render();
        void set_main_camera(std::weak_ptr<camera_t> camera);

        void register_substance(std::shared_ptr<substance_t> substance);
        void unregister_substance(std::shared_ptr<substance_t> substance);

        int get_frame_count();
    };
}

#endif
