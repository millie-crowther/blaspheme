#include "render/renderer.h"

#include "core/blaspheme.h"
#include "input.h"
#include <chrono>
#include <stdexcept>

renderer_t::renderer_t(
    VkPhysicalDevice physical_device, VkDevice device,
    VkSurfaceKHR surface, uint32_t graphics_family, 
    uint32_t present_family, VkExtent2D window_extents
){
    current_frame = 0;
    this->surface = surface;
    this->window_extents = window_extents;
    this->graphics_family = graphics_family;
    this->present_family = present_family;
    this->physical_device = physical_device;
    this->device = device;

    push_constants.window_size = vec_t<uint32_t, 2>(window_extents.width, window_extents.height);
    
    if (!init()){
        throw std::runtime_error("Error: Failed to initialise renderer subsystem.");
    }
}

renderer_t::~renderer_t(){
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);

    uniform_buffers.clear();

    cleanup_swapchain();

    for (int i = 0; i < frames_in_flight; i++){
        vkDestroySemaphore(device, image_available_semas[i], nullptr);
        vkDestroySemaphore(device, render_finished_semas[i], nullptr);
        vkDestroyFence(device, in_flight_fences[i], nullptr);
    }

    vkDestroyCommandPool(device, command_pool, nullptr); 
}

bool
renderer_t::init(){
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
    vkGetDeviceQueue(device, present_family, 0, &present_queue);

    if (!create_swapchain()){
        return false;
    }

    if (!create_render_pass()){
        return false;
    }

    if (!create_descriptor_set_layout()){
        return false;
    }
    
    if (!create_graphics_pipeline()){
        return false;
    }

    if (!create_command_pool()){
        return false;
    }

    if (!create_depth_resources()){
        return false;
    }

    if (!create_framebuffers()){
        return false;
    }

    if (!create_descriptor_pool()){
        return false;
    }

    if (!create_sync()){
        return false;
    }

    std::vector<vec_t<float, 2>> vertices = {
        vec_t<float, 2>(-1.0f, -1.0f), 
        vec_t<float, 2>(-1.0f,  1.0f),
        vec_t<float, 2>( 1.0f, -1.0f),
        vec_t<float, 2>( 1.0f,  1.0f)
    };

    std::vector<uint32_t> indices = { 0, 1, 2, 1, 3, 2 };
    mesh = std::make_shared<mesh_t>(command_pool, graphics_queue, vertices, indices);

    if (!create_command_buffers(mesh)){
        return false;
    }

    return true;
}

bool
renderer_t::create_swapchain(){
    VkSurfaceFormatKHR format = select_surface_format();
    VkPresentModeKHR mode = select_present_mode();
    VkExtent2D extents = select_swap_extent();

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device, surface, &capabilities
    );
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount){
	    image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface          = surface;
    create_info.minImageCount    = image_count;
    create_info.imageFormat      = format.format;
    create_info.imageColorSpace  = format.colorSpace;
    create_info.imageExtent      = extents;
    create_info.imageArrayLayers = 1;
    // if you dont wanna draw to image directly VK_IMAGE_USAGE_TRANSFER_DST_BIT
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;     

    uint32_t families[2] = { graphics_family, present_family };

    if (families[0] != families[1]){
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
	    create_info.pQueueFamilyIndices   = families;
    } else {
	    create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
	    create_info.pQueueFamilyIndices   = nullptr;
    }

    create_info.preTransform   = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode    = mode;
    create_info.clipped        = VK_TRUE;

    // will need to update this field if creating a new swap chain e.g. for resized window
    create_info.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain) != VK_SUCCESS){
	    return false;
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);

    VkImage swapchain_imgs[count];
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapchain_imgs);
  
    swapchain_images.clear();
    for (auto & swapchain_image : swapchain_imgs){
        swapchain_images.push_back(std::make_unique<image_t>(
            swapchain_image, format.format, VK_IMAGE_ASPECT_COLOR_BIT
        ));
    }

    swapchain_extents = extents;

    return true;
}

VkExtent2D
renderer_t::select_swap_extent(){
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device, surface, &capabilities
    );

    // check if we need to supply width and height
    if (capabilities.currentExtent.width == ~((uint32_t) 0)){
        VkExtent2D extents = window_extents;
        
        extents.width = std::max(
            capabilities.minImageExtent.width, 
            std::min(extents.width, capabilities.maxImageExtent.width)
        );
        extents.height = std::max(
            capabilities.minImageExtent.height, 
            std::min(extents.height, capabilities.maxImageExtent.height)
        );
            
        return extents;
    } else {
        return capabilities.currentExtent;
    }
}

VkPresentModeKHR
renderer_t::select_present_mode(){
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &count, nullptr
    );
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &count, modes.data()
    );

    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()){
	    return VK_PRESENT_MODE_MAILBOX_KHR;
    }

    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()){
	    return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkSurfaceFormatKHR
renderer_t::select_surface_format(){
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &count, nullptr
    );
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &count, formats.data()
    );
    
    // check if all formats supported
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED){
	    return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    }

    // check for preferred
    for (auto available_format : formats){
        if (
            available_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        ){
            return available_format;
        }
    }

    // default
    return formats[0];
}

void
renderer_t::recreate_swapchain(){
    vkDeviceWaitIdle(device);
  
    cleanup_swapchain();
    
    create_swapchain();
    create_render_pass();
    create_graphics_pipeline();
    create_depth_resources();
    create_framebuffers();

    create_command_buffers(mesh);
}

void 
renderer_t::cleanup_swapchain(){
    depth_image.reset(nullptr);

    for (auto framebuffer : swapchain_framebuffers){
	    vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vkFreeCommandBuffers(
        device, command_pool, static_cast<uint32_t>(command_buffers.size()), command_buffers.data()
    );

    vkDestroyPipeline(device, graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);

    swapchain_images.clear();

    vkDestroySwapchainKHR(device, swapchain, nullptr);
}

bool
renderer_t::create_render_pass(){
    VkAttachmentDescription colour_attachment = {};
    colour_attachment.format = swapchain_images[0]->get_format();
    colour_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colour_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colour_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colour_attachment_ref = {};
    colour_attachment_ref.attachment = 0;
    colour_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = image_t::find_depth_format(physical_device);
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass    = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colour_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                             | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = { colour_attachment, depth_attachment };

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = attachments.size();
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    return vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) == VK_SUCCESS;
}

bool 
renderer_t::create_graphics_pipeline(){
    auto vertex_shader_code   = input_t::load_file("../src/shaders/shader.vert");
    auto fragment_shader_code = input_t::load_file("../src/shaders/shader.frag");

    if (vertex_shader_code.size() == 0 || fragment_shader_code.size() == 0){
	    return false;
    }

    bool success = true;
    VkShaderModule vert_shader_module = create_shader_module(vertex_shader_code, &success);
    VkShaderModule frag_shader_module = create_shader_module(fragment_shader_code, &success);

    if (!success){
	    return false;
    }

    VkPipelineShaderStageCreateInfo vert_create_info = {}; 
    vert_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_create_info.module = vert_shader_module;
    vert_create_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_create_info = {}; 
    frag_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_create_info.module = frag_shader_module;
    frag_create_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        vert_create_info,
        frag_create_info
    };

    VkVertexInputBindingDescription binding_desc = {};
    binding_desc.binding   = 0;
    binding_desc.stride    = sizeof(vec_t<float, 2>);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr_desc = {};
    attr_desc.binding  = 0;
    attr_desc.location = 0;
    attr_desc.format   = VK_FORMAT_R32G32B32_SFLOAT;
    attr_desc.offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 1;
    vertex_input_info.pVertexAttributeDescriptions = &attr_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x          = 0;
    viewport.y          = 0;
    viewport.width      = (float) swapchain_extents.width;
    viewport.height     = (float) swapchain_extents.height;
    viewport.minDepth   = 0;
    viewport.maxDepth   = 1;

    VkRect2D scissor = {};
    scissor.offset   = {0, 0};
    scissor.extent   = swapchain_extents;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster_info = {};
    raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_info.depthClampEnable = VK_FALSE;
    raster_info.rasterizerDiscardEnable = VK_FALSE;
    raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    raster_info.lineWidth = 1.0f;
    raster_info.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_info.depthBiasEnable = VK_FALSE;
    raster_info.depthBiasConstantFactor = 0;
    raster_info.depthBiasClamp = 0;
    raster_info.depthBiasSlopeFactor = 0;
    
    VkPipelineMultisampleStateCreateInfo multisample_info = {};
    multisample_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_info.sampleShadingEnable = VK_FALSE;
    multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample_info.minSampleShading = 1.0f;
    multisample_info.pSampleMask = nullptr;
    multisample_info.alphaToCoverageEnable = VK_FALSE;
    multisample_info.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colour_blending = {};
    colour_blending.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                   | VK_COLOR_COMPONENT_G_BIT
                                   | VK_COLOR_COMPONENT_B_BIT
                                   | VK_COLOR_COMPONENT_A_BIT;
    colour_blending.blendEnable = VK_FALSE;
    colour_blending.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colour_blending.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colour_blending.colorBlendOp = VK_BLEND_OP_ADD;
    colour_blending.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colour_blending.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colour_blending.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colour_blend_info = {};
    colour_blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colour_blend_info.logicOpEnable = VK_FALSE;
    colour_blend_info.logicOp = VK_LOGIC_OP_COPY;
    colour_blend_info.attachmentCount = 1;
    colour_blend_info.pAttachments = &colour_blending;
    colour_blend_info.blendConstants[0] = 0.0f;
    colour_blend_info.blendConstants[1] = 0.0f;
    colour_blend_info.blendConstants[2] = 0.0f;
    colour_blend_info.blendConstants[3] = 0.0f;
    const VkPipelineColorBlendStateCreateInfo colour_blend_const = colour_blend_info;

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(
	    device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS
    ){
	    return false;
    }

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS; 
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.minDepthBounds = 0.0f; 
    depth_stencil.maxDepthBounds = 1.0f;
    depth_stencil.stencilTestEnable = VK_FALSE;
    depth_stencil.front = {};
    depth_stencil.back = {};

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &raster_info;
    pipeline_info.pMultisampleState = &multisample_info;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &colour_blend_const;
    pipeline_info.pDynamicState = nullptr;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    VkResult result = vkCreateGraphicsPipelines(
	    device, VK_NULL_HANDLE, 1, 
        &pipeline_info, nullptr, &graphics_pipeline
    );

    if (result != VK_SUCCESS){
	    return false;
    }

    vkDestroyShaderModule(device, vert_shader_module, nullptr);
    vkDestroyShaderModule(device, frag_shader_module, nullptr);

    return true;
}

bool
renderer_t::create_depth_resources(){
    VkFormat depth_format = image_t::find_depth_format(physical_device);

    depth_image = std::make_unique<image_t>(
        swapchain_extents.width, swapchain_extents.height, depth_format,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT
    );

    depth_image->transition_image_layout(
        command_pool, graphics_queue, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    );
    return true;
}

bool
renderer_t::create_framebuffers(){
    swapchain_framebuffers.resize(swapchain_images.size());

    for (int i = 0; i < swapchain_framebuffers.size(); i++){
        std::array<VkImageView, 2> attachments = {
	        swapchain_images[i]->get_image_view(),
            depth_image->get_image_view()
	    };

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = attachments.size();
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = swapchain_extents.width;
        framebuffer_info.height = swapchain_extents.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(
            device, &framebuffer_info, nullptr, &swapchain_framebuffers[i]) != VK_SUCCESS
        ){
            return false;
        }
    }

    return true;
}

bool
renderer_t::create_command_buffers(std::shared_ptr<mesh_t> mesh){
    // create command buffers
    command_buffers.resize(swapchain_framebuffers.size());
    
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t) command_buffers.size();

    if (vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data()) != VK_SUCCESS){
        return false;
    }

    for (int i = 0; i < command_buffers.size(); i++){
        VkCommandBufferBeginInfo begin_info = {};
   	    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	    begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(command_buffers[i], &begin_info) != VK_SUCCESS){
            return false;
        }

        VkRenderPassBeginInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = render_pass;
        render_pass_info.framebuffer = swapchain_framebuffers[i];
        render_pass_info.renderArea.offset = { 0, 0 };
        render_pass_info.renderArea.extent = swapchain_extents;

        std::array<VkClearValue, 2> clear_values = {};
        clear_values[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        clear_values[1].depthStencil = { 1.0f, 0 };

        render_pass_info.clearValueCount = clear_values.size();
        render_pass_info.pClearValues = clear_values.data();

        vkCmdBeginRenderPass(command_buffers[i], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdPushConstants(
                command_buffers[i],
                pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(push_constants),
                &push_constants
            );

            vkCmdBindPipeline(
                command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline
            );

            VkBuffer vertex_buffers[1] = { mesh->get_vertex_buffer()->get_buffer() };
            VkDeviceSize offset = 0;
	        vkCmdBindVertexBuffers(command_buffers[i], 0, 1, vertex_buffers, &offset);
            vkCmdBindIndexBuffer(
                command_buffers[i], mesh->get_index_buffer()->get_buffer(), 0, VK_INDEX_TYPE_UINT32
            );
            vkCmdBindDescriptorSets(
		        command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		        0, 1, &desc_sets[i], 0, nullptr
	        );

	        vkCmdDrawIndexed(command_buffers[i], (uint32_t) mesh->get_index_count(), 1, 0, 0, 0);
	    vkCmdEndRenderPass(command_buffers[i]);

        if (vkEndCommandBuffer(command_buffers[i]) != VK_SUCCESS){
	        return false;
	    }
    }

    return true;
}


bool 
renderer_t::create_descriptor_pool(){
    std::array<VkDescriptorPoolSize, 2> pool_sizes = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = static_cast<uint32_t>(swapchain_images.size());

    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = static_cast<uint32_t>(swapchain_images.size());

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = static_cast<uint32_t>(swapchain_images.size());

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool) != VK_SUCCESS){
	    return false;
    }

    if (!create_descriptor_sets()){
	    return false;
    }

    return true;
}

bool 
renderer_t::create_descriptor_sets(){
    int n = swapchain_images.size();
    std::vector<VkDescriptorSetLayout> layouts(n, descriptor_layout);

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool;
    alloc_info.descriptorSetCount = static_cast<uint32_t>(n);
    alloc_info.pSetLayouts = layouts.data();

    desc_sets.resize(n);
    if (vkAllocateDescriptorSets(device, &alloc_info, desc_sets.data()) != VK_SUCCESS){
	    return false;
    }

    return true;
}

bool
renderer_t::create_descriptor_set_layout(){
    VkDescriptorSetLayoutBinding ubo_layout = {};
    ubo_layout.binding = 0;
    ubo_layout.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_layout.descriptorCount = 1;
    ubo_layout.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ubo_layout.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_layout;

    VkResult result = vkCreateDescriptorSetLayout(
        device, &layout_info, nullptr, &descriptor_layout
    );
 
    return result == VK_SUCCESS;
}

bool
renderer_t::create_command_pool(){
    // create command pool
    VkCommandPoolCreateInfo command_pool_info = {};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = graphics_family;
    command_pool_info.flags = 0;

    if (vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool) != VK_SUCCESS){
    	return false;
    }

    return true;
}

bool
renderer_t::create_sync(){
    image_available_semas.resize(frames_in_flight);
    render_finished_semas.resize(frames_in_flight);
    in_flight_fences.resize(frames_in_flight);

    VkSemaphoreCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
   
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
 
    for (int i = 0; i < frames_in_flight; i++){
        if (vkCreateSemaphore(
            device, &create_info, nullptr, &image_available_semas[i]) != VK_SUCCESS
        ){
            return false;
        }

        if (vkCreateSemaphore(
            device, &create_info, nullptr, &render_finished_semas[i]) != VK_SUCCESS
        ){
            return false;
        }

        if (vkCreateFence(device, &fence_info, nullptr, &in_flight_fences[i]) != VK_SUCCESS){
            return false;
        }
    }

    return true;
}


void
renderer_t::render(){
    vkWaitForFences(device, 1, &in_flight_fences[current_frame], VK_TRUE, ~((uint64_t) 0));
    vkResetFences(device, 1, &in_flight_fences[current_frame]); 

    uint32_t image_index;
    vkAcquireNextImageKHR(
        device, swapchain, ~((uint64_t) 0), image_available_semas[current_frame], 
        VK_NULL_HANDLE, &image_index
    );
   
    VkSemaphore wait_semas[1] = { image_available_semas[current_frame] };
    VkPipelineStageFlags wait_stages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submit_info = {};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semas;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[image_index];
    
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished_semas[current_frame];

    VkResult res = vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame]);

    if (res == VK_ERROR_OUT_OF_DATE_KHR){
        //recreate_swapchain();
        return;
    } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR){
        throw std::runtime_error("Error: Failed to submit to draw command buffer.");
    }

    VkPresentInfoKHR present_info   = {};
    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = &render_finished_semas[current_frame];
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = &swapchain;
    present_info.pImageIndices      = &image_index;
    present_info.pResults           = nullptr;
    
    res = vkQueuePresentKHR(present_queue, &present_info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR){ 
        //recreate_swapchain();
    } else if (res != VK_SUCCESS){
        throw std::runtime_error("Error: Failed to present image.");
    }

    current_frame = (current_frame + 1) % frames_in_flight;    
}

VkShaderModule
renderer_t::create_shader_module(const std::vector<char> & code, bool * success){
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; 
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shader_module;
    if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) != VK_SUCCESS){
	    *success = false;
    }
    return shader_module;
}

void
renderer_t::window_resize(uint32_t width, uint32_t height){
    window_extents = { width, height };
    push_constants.window_size = vec_t<uint32_t, 2>(width, height);
    recreate_swapchain();
}
