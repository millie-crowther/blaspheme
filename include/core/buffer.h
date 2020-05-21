#ifndef BUFFER_H
#define BUFFER_H

#include <cstring>
#include <memory>

#include "core/command.h"
#include "core/device.h"
#include "maths/vec.h"

template<bool is_device_local>
class buffer_t {
private:
    std::shared_ptr<device_t> device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint32_t size;
    uint32_t binding;
    VkDescriptorBufferInfo desc_buffer_info;
    std::unique_ptr<buffer_t<false>> staging_buffer;
    std::vector<VkBufferCopy> updates;
    VkBufferCopy read_buffer_copy;

public:
    // constructors and destructors
    buffer_t(uint32_t binding, std::shared_ptr<device_t> device, uint64_t size){
        this->device = device;
        this->size = size;
        this->binding = binding;

        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkMemoryPropertyFlagBits memory_property;

        if constexpr (is_device_local){
            buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            memory_property = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        } else {
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            memory_property = static_cast<VkMemoryPropertyFlagBits>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }

        if (vkCreateBuffer(device->get_device(), &buffer_info, nullptr, &buffer) != VK_SUCCESS){
            throw std::runtime_error("Error: Failed to create buffer.");
        }

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(device->get_device(), buffer, &mem_req);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = find_memory_type(device, mem_req.memoryTypeBits, memory_property);

        if (vkAllocateMemory(device->get_device(), &alloc_info, nullptr, &memory) != VK_SUCCESS){
            throw std::runtime_error("Error: Failed to allocate buffer memory.");
        } 

        vkBindBufferMemory(device->get_device(), buffer, memory, 0); 

        desc_buffer_info = {};
        desc_buffer_info.buffer = buffer;
        desc_buffer_info.offset = 0;
        desc_buffer_info.range  = size;

        if constexpr (is_device_local){
            staging_buffer = std::make_unique<buffer_t<false>>(~0, device, size);
        }

        read_buffer_copy.srcOffset = 0;
        read_buffer_copy.dstOffset = 0;
        read_buffer_copy.size = size;
    }
    
    ~buffer_t(){
        vkDestroyBuffer(device->get_device(), buffer, nullptr);
        vkFreeMemory(device->get_device(), memory, nullptr);
    }    
    
    template<class F>
    void map(uint64_t offset, uint64_t size, const F & f){
        void * memory_map;
        vkMapMemory(device->get_device(), memory, offset, size, 0, &memory_map);
        f(memory_map);
        vkUnmapMemory(device->get_device(), memory);
    }

    // public methods
    template<class T>
    void write(const T & source, uint64_t offset){
        if (source.empty()){
            return;
        }

        uint32_t size = sizeof(typename T::value_type) * source.size();

        if constexpr (is_device_local){
            staging_buffer->write(source, offset);

            VkBufferCopy buffer_copy;
            buffer_copy.srcOffset = offset;
            buffer_copy.dstOffset = offset;
            buffer_copy.size = size;
            updates.push_back(buffer_copy);
        } else {
            map(offset, size, [&](void * memory_map){
                std::memcpy(memory_map, source.data(), size);
            });
        }
    }

    void record_write(VkCommandBuffer command_buffer){ 
        vkCmdCopyBuffer(command_buffer, staging_buffer->get_buffer(), buffer, updates.size(), updates.data());
        updates.clear();
    }

    void record_read(VkCommandBuffer command_buffer) const { 
        vkCmdCopyBuffer(command_buffer, buffer, staging_buffer->get_buffer(), 1, &read_buffer_copy);
    }

    template<class T>
    void read(T & destination, uint64_t offset) {
        if (destination.empty()){
            return;
        }

        uint32_t size = sizeof(typename T::value_type) * destination.size();
        staging_buffer->map(offset, size, [&](void * memory_map){
            std::memcpy(destination.data(), memory_map, size);
        });
    }
    
    VkWriteDescriptorSet get_write_descriptor_set(VkDescriptorSet descriptor_set) const {
        VkWriteDescriptorSet write_desc_set = {};
        write_desc_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_desc_set.pNext = nullptr;
        write_desc_set.dstArrayElement = 0;
        write_desc_set.descriptorCount = 1;
        write_desc_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write_desc_set.pImageInfo = nullptr;
        write_desc_set.pTexelBufferView = nullptr;
        write_desc_set.dstSet = descriptor_set;
        write_desc_set.dstBinding = binding;
        write_desc_set.pBufferInfo = &desc_buffer_info;
        return write_desc_set;
    }
    
    VkDescriptorSetLayoutBinding get_descriptor_set_layout_binding() const {
        VkDescriptorSetLayoutBinding layout_binding = {};
        layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layout_binding.descriptorCount = 1;
        layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layout_binding.pImmutableSamplers = nullptr;
        layout_binding.binding = binding;
        return layout_binding;
    }

    VkBuffer get_buffer() const {
        return buffer;
    }

    static uint32_t find_memory_type(std::shared_ptr<device_t> device, uint32_t type_filter, VkMemoryPropertyFlags prop){
        VkPhysicalDeviceMemoryProperties mem_prop;
        vkGetPhysicalDeviceMemoryProperties(device->get_physical_device(), &mem_prop);

        for (uint32_t i = 0; i < mem_prop.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) && (mem_prop.memoryTypes[i].propertyFlags & prop) == prop) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }
};

typedef buffer_t<false> host_buffer_t;
typedef buffer_t<true> device_buffer_t;

#endif
