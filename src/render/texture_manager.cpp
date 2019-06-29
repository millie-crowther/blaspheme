#include "render/texture_manager.h"

#include "core/vk_utils.h"

#include "render/brick.h"

constexpr uint8_t texture_manager_t::brick_size;

texture_manager_t::texture_manager_t(const allocator_t & allocator, uint16_t grid_size, const std::vector<VkDescriptorSet> & desc_sets){
    this->grid_size = grid_size;
    this->allocator = allocator;
    claimed_bricks = 0;
    
    u32vec2_t image_size(grid_size * brick_size);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaMemoryUsage vma_usage = VMA_MEMORY_USAGE_GPU_ONLY;
    

    colour_texture = std::make_unique<texture_t>(allocator, image_size, usage, vma_usage);


    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = colour_texture->get_image_view();
    image_info.sampler = colour_texture->get_sampler();

    VkWriteDescriptorSet descriptor_write = {};
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstBinding = 2;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pImageInfo = &image_info;

    for (auto desc_set : desc_sets){
        descriptor_write.dstSet = desc_set;
        vkUpdateDescriptorSets(allocator.device, 1, &descriptor_write, 0, nullptr);
    }

    staging_buffer = std::make_unique<buffer_t>(
        allocator, brick_size * brick_size * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    colour_texture->transition_image_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

u16vec2_t 
texture_manager_t::request(const std::array<colour_t, brick_size * brick_size> & brick){
    u16vec2_t uv;

    if (claimed_bricks < static_cast<uint32_t>(grid_size * grid_size)){
        claimed_bricks++;
        uv = u16vec2_t(
            static_cast<uint16_t>(claimed_bricks % grid_size), 
            static_cast<uint16_t>(claimed_bricks / grid_size)
        );

    } else if (!bricks.empty()){
        uv = bricks.front();
        bricks.pop();

    } else {
        throw std::runtime_error("No brick textures left!!");
    }

    staging_buffer->copy(brick.data(), brick.size() * sizeof(u8vec4_t), 0);

    staging_buffer->copy_to_image(
        colour_texture->get_image(), 
        uv.cast<uint32_t>() * brick_size,
        u32vec2_t(brick_size)
    );

    return uv;
}

void 
texture_manager_t::clear(u16vec2_t brick){
    bricks.push(brick);
}