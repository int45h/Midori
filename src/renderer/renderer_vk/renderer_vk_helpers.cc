#include <renderer_vk/renderer_vk_helpers.h>
#include <vulkan/vulkan_core.h>

#pragma region [ Render Context ]
MdResult mdInitContext(MdRenderContext &context, const std::vector<const char*> &instance_extensions)
{
    // Create the vulkan instance
    vkb::InstanceBuilder instance_builder;
    auto ret_instance = instance_builder
        .enable_extensions(instance_extensions)
        .require_api_version(VK_API_VERSION_1_3)
        .request_validation_layers()
        .use_default_debug_messenger()
        .build();
    if (!ret_instance)
    {
        LOG_ERROR("failed to create vulkan instance: %s\n", ret_instance.error().message().c_str());
        return MD_ERROR_VULKAN_INSTANCE_FAILURE;
    }
    context.instance = ret_instance.value();
    context.api_version = VK_API_VERSION_1_3;

    return MD_SUCCESS;
}

void mdDestroyContext(MdRenderContext &context)
{
    if (context.sw_image_views.size() > 0)
    {
        for (u32 i=0; i<context.sw_image_views.size(); i++)
        {
            if (context.sw_image_views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(context.device, context.sw_image_views[i], NULL);
        }
    }

    vkb::destroy_swapchain(context.swapchain);
    vkb::destroy_device(context.device);
    if (context.surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(context.instance, context.surface, NULL);
    vkb::destroy_instance(context.instance);
}

MdResult mdCreateDevice(MdRenderContext &context)
{
    vkb::PhysicalDeviceSelector device_selector(context.instance);
    auto pdev_ret = device_selector
        .set_surface(context.surface)
        .set_minimum_version(1, 1)
        .select();
    
    if (!pdev_ret)
    {
        LOG_ERROR("failed to get physical device: %s\n", pdev_ret.error().message().c_str());
        return MD_ERROR_VULKAN_PHYSICAL_DEVICE_FAILURE;
    }
    vkb::DeviceBuilder device_builder(pdev_ret.value());
    auto dev_ret = device_builder.build();
    if (!dev_ret)
    {
        LOG_ERROR("failed to create logical device: %s\n", dev_ret.error().message().c_str());
        return MD_ERROR_VULKAN_LOGICAL_DEVICE_FAILURE;
    }
    context.physical_device = pdev_ret.value();
    context.device = dev_ret.value();

    return MD_SUCCESS;
}

MdResult mdGetQueue(VkQueueFlagBits queue_type, MdRenderContext &context, MdRenderQueue &queue)
{
    vkb::QueueType type;
    switch (queue_type)
    {
        case VK_QUEUE_GRAPHICS_BIT: type = vkb::QueueType::graphics; break;
        case VK_QUEUE_COMPUTE_BIT: type = vkb::QueueType::compute; break;
        case VK_QUEUE_TRANSFER_BIT: type = vkb::QueueType::transfer; break;
        default: LOG_ERROR("queue type is unsupported\n"); return MD_ERROR_VULKAN_QUEUE_NOT_PRESENT;
    }
    
    auto queue_ret = context.device.get_queue(type);
    if (!queue_ret)
    {
        LOG_ERROR("failed to get queue: %s\n", queue_ret.error().message().c_str());
        return MD_ERROR_VULKAN_QUEUE_NOT_PRESENT;
    }
    i32 index = context.device.get_queue_index(type).value();
    
    queue = MdRenderQueue(queue_ret.value(), index);
    return MD_SUCCESS;
}

MdResult mdGetSwapchain(MdRenderContext &context, bool rebuild)
{
    // Get swapchain
    vkb::SwapchainBuilder sw_builder(context.device);
    auto sw_ret = (rebuild) ? sw_builder.set_old_swapchain(context.swapchain).build() : sw_builder.build();
    
    if (!sw_ret)
    {
        LOG_ERROR("failed to get swapchain: %s\n", sw_ret.error().message().c_str());
        return MD_ERROR_VULKAN_SWAPCHAIN_FAILURE;
    }
    context.swapchain = sw_ret.value();
    
    // Create image views and framebuffers
    auto images = context.swapchain.get_images();
    if (!images)
    {
        LOG_ERROR("failed to get image views: %s\n", images.error().message().c_str());
        return MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_VIEW_FAILURE;
    }

    auto views = context.swapchain.get_image_views();
    if (!views)
    {
        LOG_ERROR("failed to get image views: %s\n", views.error().message().c_str());
        return MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_VIEW_FAILURE;
    }

    context.sw_images = images.value();
    context.sw_image_views = views.value();

    return MD_SUCCESS;
}

//VkResult mdRebuildSwapchain(MdRenderContext &context, u16 w, u16 h)
//{
//    VkResult result = VK_ERROR_UNKNOWN;
//
//    if (context.swapchain.swapchain != VK_NULL_HANDLE)
//        vkb::destroy_swapchain(context.swapchain);
//
//    mdGetSwapchain(context);
//
//    return result;
//}
#pragma endregion

#pragma region [ Command Encoder ]
VkResult mdCreateCommandEncoder(MdRenderContext &context, u32 queue_family_index, MdCommandEncoder &encoder, VkCommandPoolCreateFlags flags)
{
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = flags;
    pool_info.queueFamilyIndex = queue_family_index;
    VkResult result = vkCreateCommandPool(context.device, &pool_info, NULL, &encoder.pool);
    VK_CHECK(result, "failed to create command pool");
    
    return result;
}

VkResult mdAllocateCommandBuffers(MdRenderContext &context, u32 buffer_count, VkCommandBufferLevel level, MdCommandEncoder &encoder)
{
    VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandBufferCount = buffer_count;
    alloc_info.commandPool =  encoder.pool;
    alloc_info.level = level;
    encoder.buffers.reserve(buffer_count);
    for (u32 i=0; i<buffer_count; i++)
        encoder.buffers.push_back(VK_NULL_HANDLE);

    VkResult result = vkAllocateCommandBuffers(context.device, &alloc_info, encoder.buffers.data());
    VK_CHECK(result, "failed to allocate command buffers");

    return result;
}

void mdDestroyCommandEncoder(MdRenderContext &context, MdCommandEncoder &encoder)
{
    if (encoder.buffers.size() > 0)
        vkFreeCommandBuffers(context.device, encoder.pool, encoder.buffers.size(), encoder.buffers.data());

    vkDestroyCommandPool(context.device, encoder.pool, NULL);
}

#pragma endregion

#pragma region [ Memory ]
VkResult mdCreateGPUAllocator(MdRenderContext &context, MdGPUAllocator &allocator, MdRenderQueue queue, VkDeviceSize staging_buffer_size)
{
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = context.instance;
    alloc_info.device = context.device;
    alloc_info.physicalDevice = context.physical_device;
    alloc_info.pAllocationCallbacks = NULL;
    alloc_info.pDeviceMemoryCallbacks = NULL;
    alloc_info.vulkanApiVersion = context.api_version;
    VkResult result = vmaCreateAllocator(&alloc_info, &allocator.allocator);
    VK_CHECK(result, "failed to create memory allocator");

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.flags = 0;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.size = staging_buffer_size;
    
    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &allocation_info, 
        &allocator.staging_buffer.buffer,
        &allocator.staging_buffer.allocation,
        &allocator.staging_buffer.allocation_info
    );
    VK_CHECK(result, "failed to allocate memory for staging buffer");

    allocator.queue = queue;
    allocator.staging_buffer.size = staging_buffer_size;
    allocator.device = context.device;
    return result;
}

VkResult mdAllocateGPUBuffer(VkBufferUsageFlags usage, u32 size, MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.flags = 0;
    buffer_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.size = size;
    
    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &allocation_info, 
        &buffer.buffer,
        &buffer.allocation,
        &buffer.allocation_info
    );
    VK_CHECK(result, "failed to allocate buffer");

    buffer.size = size;
    buffer.free = false;
    return result;
}

VkResult mdAllocateGPUUniformBuffer(u32 size, MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.flags = 0;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.size = size;
    
    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkResult result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &allocation_info, 
        &buffer.buffer,
        &buffer.allocation,
        &buffer.allocation_info
    );
    VK_CHECK(result, "failed to allocate buffer");

    buffer.size = size;
    buffer.free = false;
    return result;
}

void mdFreeGPUBuffer(MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    if (buffer.free == true)
        return;
    
    vmaDestroyBuffer(allocator.allocator, buffer.buffer, buffer.allocation);
    buffer.free = false;
}

void mdFreeUniformBuffer(MdGPUAllocator &allocator, MdGPUBuffer &buffer)
{
    if (buffer.free == true)
        return;
    
    vmaUnmapMemory(allocator.allocator, buffer.allocation);
    vmaDestroyBuffer(allocator.allocator, buffer.buffer, buffer.allocation);
    buffer.free = false;
}

VkResult mdUploadToGPUBuffer(   MdRenderContext &context, 
                                MdGPUAllocator &allocator, 
                                u32 offset, 
                                u32 range, 
                                const void *p_data, 
                                MdGPUBuffer &buffer, 
                                MdCommandEncoder *p_command_encoder,
                                u32 command_buffer_index)
{
    u32 size = range - offset;
    u32 block_size = allocator.staging_buffer.size;
    u32 block_count = (size / block_size) + 1;
    
    VkCommandBuffer cmd_buffer;
    VkResult result = VK_ERROR_UNKNOWN;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    // If the user did not pass their own command encoder, create our own temporary one
    bool active_recording = (p_command_encoder != NULL);
    if (!active_recording)
    {
        VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = allocator.queue.queue_index;
        
        result = vkCreateCommandPool(context.device, &pool_info, NULL, &cmd_pool);
        VK_CHECK(result, "failed to create command pool");

        VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_alloc_info.commandBufferCount = 1;
        cmd_alloc_info.commandPool = cmd_pool;
        cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        result = vkAllocateCommandBuffers(context.device, &cmd_alloc_info, &cmd_buffer);
        VK_CHECK(result, "failed to allocate command buffers");
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];

    // Set up data pointers
    void *dst = allocator.staging_buffer.allocation_info.pMappedData; 
    u8 *data_ptr = (u8*)p_data;
    
    if (!active_recording)
    {
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }

    i32 current_size = size;
    for (u32 i=0; i<block_count; i++)
    {
        current_size = current_size - block_size*i;
        if (current_size < 0) 
            current_size = 0;
        
        memcpy(dst, (data_ptr+(size-current_size)), MIN_VAL(current_size, block_size));
        VkBufferCopy copy = {};
        copy.srcOffset = 0;
        copy.dstOffset = size - current_size;
        copy.size = MIN_VAL(current_size, block_size);
        
        vkCmdCopyBuffer(
            cmd_buffer, 
            allocator.staging_buffer.buffer, 
            buffer.buffer, 
            1,
            &copy
        );
    }

    if (!active_recording)
    {
        vkEndCommandBuffer(cmd_buffer);
        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;
        
        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        vkFreeCommandBuffers(context.device, cmd_pool, 1, &cmd_buffer);
        vkDestroyCommandPool(context.device, cmd_pool, NULL);
    }

    return result;
}

VkResult mdUploadToUniformBuffer(   MdRenderContext &context, 
                                    MdGPUAllocator &allocator, 
                                    u32 offset, 
                                    u32 range, 
                                    const void *p_data, 
                                    MdGPUBuffer &buffer)
{
    u32 size = range - offset;
    if (size > (buffer.size))
    {
        LOG_ERROR(
            "the size of the memory region (%d) exceeds the size of the uniform buffer. (%d)\n",
            size,
            buffer.size
        );
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    
    void *p_mapped = NULL;
    VkResult result = vmaMapMemory(
        allocator.allocator, 
        buffer.allocation, 
        &p_mapped
    );
    VK_CHECK(result, "failed to map memory");

    memcpy(p_mapped, p_data, size);
    vmaUnmapMemory(allocator.allocator, buffer.allocation);
    return result;
}
#pragma endregion

#pragma region [ Textures (Related to memory) ]

u64 mdGetTextureSize(MdGPUTexture &texture)
{
    return texture.h * texture.w * texture.channels;
}

void mdTransitionImageLayout(   MdGPUTexture &texture, 
                                VkImageLayout src_layout, 
                                VkImageLayout dst_layout, 
                                VkAccessFlags src_access,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags src_stage,
                                VkPipelineStageFlags dst_stage,
                                VkCommandBuffer buffer)
{
    VkImageMemoryBarrier image_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image_barrier.image = texture.image;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    
    image_barrier.oldLayout = src_layout;
    image_barrier.newLayout = dst_layout;
    image_barrier.srcAccessMask = src_access;
    image_barrier.dstAccessMask = dst_access;

    image_barrier.subresourceRange = texture.subresource;

    vkCmdPipelineBarrier(
        buffer, 
        src_stage, 
        dst_stage, 
        0, 
        0, 
        NULL, 
        0, 
        NULL, 
        1, 
        &image_barrier
    );
}

void mdTransitionImageLayoutWaitEvent(  MdGPUTexture &texture, 
                                        VkImageLayout src_layout, 
                                        VkImageLayout dst_layout, 
                                        VkAccessFlags src_access, 
                                        VkAccessFlags dst_access, 
                                        VkPipelineStageFlags src_stage, 
                                        VkPipelineStageFlags dst_stage, 
                                        VkCommandBuffer buffer,
                                        VkEvent *p_event)
{
    VkImageMemoryBarrier image_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image_barrier.image = texture.image;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    
    image_barrier.oldLayout = src_layout;
    image_barrier.newLayout = dst_layout;
    image_barrier.srcAccessMask = src_access;
    image_barrier.dstAccessMask = dst_access;

    image_barrier.subresourceRange = texture.subresource;

    vkCmdWaitEvents(
        buffer, 
        1, 
        p_event, 
        src_stage, 
        dst_stage, 
        0, 
        NULL, 
        0, 
        NULL, 
        1, 
        &image_barrier
    );
}

void mdCreateTextureBuilder2D(MdGPUTextureBuilder &builder, u16 w, u16 h, VkFormat format, VkImageAspectFlags aspect, u16 channels)
{
    builder.image_info.flags = 0;
    builder.image_info.imageType = VK_IMAGE_TYPE_2D;
    builder.image_info.format = format;
    builder.image_info.extent = {w, h, 1};
    builder.image_info.mipLevels = 1;
    builder.image_info.arrayLayers = 1;
    builder.image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    builder.image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    builder.image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    builder.image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    builder.image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    
    builder.image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    builder.image_view_info.subresourceRange.aspectMask = aspect;
    builder.image_view_info.subresourceRange.baseArrayLayer = 0;
    builder.image_view_info.subresourceRange.baseMipLevel = 0;
    builder.image_view_info.subresourceRange.layerCount = 1;
    builder.image_view_info.subresourceRange.levelCount = 1;
    builder.image_view_info.format = format;
    builder.image_view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, 
        VK_COMPONENT_SWIZZLE_IDENTITY, 
        VK_COMPONENT_SWIZZLE_IDENTITY, 
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    builder.image_view_info.flags = 0;
    builder.image_view_info.image = VK_NULL_HANDLE;
    
    builder.sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    builder.sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    builder.sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    builder.sampler_info.anisotropyEnable = VK_FALSE;
    builder.sampler_info.maxAnisotropy = 1;
    builder.sampler_info.unnormalizedCoordinates = VK_FALSE;
    builder.sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    builder.sampler_info.compareEnable = VK_FALSE;
    builder.sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    builder.sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    builder.sampler_info.mipLodBias = 0;
    builder.sampler_info.minLod = 0;
    builder.sampler_info.maxLod = 0;
    builder.sampler_info.magFilter = VK_FILTER_LINEAR;
    builder.sampler_info.minFilter = VK_FILTER_LINEAR;

    builder.channels = channels;
}

VkResult mdBuildTexture2D(  MdRenderContext &context, 
                            MdGPUTextureBuilder &tex_builder,
                            MdGPUAllocator &allocator,
                            MdGPUTexture &texture, 
                            const void *data,
                            MdCommandEncoder *p_command_encoder,
                            u32 command_buffer_index)
{
    texture.channels = tex_builder.channels;
    texture.w = tex_builder.image_info.extent.width;
    texture.h = tex_builder.image_info.extent.height;
    texture.format = tex_builder.image_info.format;
    texture.subresource = tex_builder.image_view_info.subresourceRange;

    VmaAllocationCreateInfo img_info = {};
    img_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    img_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    img_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkResult result = vmaCreateImage(
        allocator.allocator, 
        &tex_builder.image_info, 
        &img_info, 
        &texture.image, 
        &texture.allocation, 
        &texture.allocation_info
    );
    VK_CHECK(result, "failed to create image allocation");
    
    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    MdGPUBuffer image_staging_buffer = {};
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.size = mdGetTextureSize(texture);

    VmaAllocationCreateInfo buf_alloc_info = {};
    buf_alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    buf_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    result = vmaCreateBuffer(
        allocator.allocator, 
        &buffer_info, 
        &buf_alloc_info, 
        &image_staging_buffer.buffer, 
        &image_staging_buffer.allocation, 
        &image_staging_buffer.allocation_info
    );
    VK_CHECK(result, "failed to create image staging buffer");

    MdCommandEncoder encoder = {};
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (p_command_encoder == NULL)
    {
        result = mdCreateCommandEncoder(
            context, 
            allocator.queue.queue_index, 
            encoder,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
        VK_CHECK(result, "failed to create command encoder");

        result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, encoder);
        VK_CHECK(result, "failed to allocate command buffers");
        cmd_buffer = encoder.buffers[0];
    
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.pInheritanceInfo = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];

    void *dst_ptr = image_staging_buffer.allocation_info.pMappedData;
    memcpy(dst_ptr, data, buffer_info.size);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageOffset = {0,0,0};
    region.imageExtent = {texture.w, texture.h, 1};
    region.imageSubresource.aspectMask = tex_builder.image_view_info.subresourceRange.aspectMask;
    region.imageSubresource.baseArrayLayer = tex_builder.image_view_info.subresourceRange.baseArrayLayer;
    region.imageSubresource.layerCount = tex_builder.image_view_info.subresourceRange.layerCount;
    region.imageSubresource.mipLevel = tex_builder.image_view_info.subresourceRange.baseMipLevel;
    
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        cmd_buffer
    );
    vkCmdCopyBufferToImage(
        cmd_buffer, 
        image_staging_buffer.buffer, 
        texture.image, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        1, 
        &region
    );
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        cmd_buffer
    );

    if (p_command_encoder == NULL)
    {
        vkEndCommandBuffer(cmd_buffer);

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        mdDestroyCommandEncoder(context, encoder);    
    }
    
    vmaDestroyBuffer(allocator.allocator, image_staging_buffer.buffer, image_staging_buffer.allocation);

    return result;
}

#define MAX_ATTACHMENT_WIDTH 8192
#define MAX_ATTACHMENT_HEIGHT 8192

VkResult mdBuildDepthAttachmentTexture2D(   MdRenderContext &context, 
                                            MdGPUTextureBuilder &tex_builder,
                                            MdGPUAllocator &allocator,
                                            MdGPUTexture &texture, 
                                            MdCommandEncoder *p_command_encoder,
                                            u32 command_buffer_index)
{
    texture.channels = tex_builder.channels;
    texture.w = tex_builder.image_info.extent.width;
    texture.h = tex_builder.image_info.extent.height;
    texture.format = tex_builder.image_info.format;
    texture.subresource = tex_builder.image_view_info.subresourceRange;
    
    VmaAllocationCreateInfo img_info = {};
    img_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    img_info.flags = (
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT |
        VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT
    );

    // Create a "backing image", with the maximum allowed width and height of the texture
    VkImage backing_image = VK_NULL_HANDLE;
    VkImageCreateInfo backing_info = tex_builder.image_info;
    backing_info.extent.width = 8192;
    backing_info.extent.height = 8192;
    
    VkResult result = vkCreateImage(context.device, &backing_info, NULL, &backing_image);
    VK_CHECK(result, "failed to create backing image");

    result = vmaAllocateMemoryForImage(
        allocator.allocator, 
        backing_image, 
        &img_info, 
        &texture.allocation, 
        &texture.allocation_info
    );
    VK_CHECK(result, "failed to allocate memory for texture");
    
    // Destroy the backing image since we no longer need it, create the actual image and bind it
    vkDestroyImage(context.device, backing_image, NULL);
    result = vkCreateImage(context.device, &tex_builder.image_info, NULL, &texture.image);
    VK_CHECK(result, "failed to create image");

    result = vmaBindImageMemory(allocator.allocator, texture.allocation, texture.image);
    VK_CHECK(result, "failed to bind image memory");

    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    MdCommandEncoder encoder = {};
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (p_command_encoder == NULL)
    {
        result = mdCreateCommandEncoder(
            context, 
            allocator.queue.queue_index, 
            encoder,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
        VK_CHECK(result, "failed to create command encoder");

        result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, encoder);
        VK_CHECK(result, "failed to allocate command buffers");
        cmd_buffer = encoder.buffers[0];
    
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.pInheritanceInfo = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];
    
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        cmd_buffer
    );
    
    if (p_command_encoder == NULL)
    {
        vkEndCommandBuffer(cmd_buffer);

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        mdDestroyCommandEncoder(context, encoder);    
    }
    
    return result;
}

VkResult mdBuildColorAttachmentTexture2D(   MdRenderContext &context, 
                                            MdGPUTextureBuilder &tex_builder,
                                            MdGPUAllocator &allocator,
                                            MdGPUTexture &texture, 
                                            MdCommandEncoder *p_command_encoder,
                                            u32 command_buffer_index)
{
    texture.channels = tex_builder.channels;
    texture.w = tex_builder.image_info.extent.width;
    texture.h = tex_builder.image_info.extent.height;
    texture.format = tex_builder.image_info.format;
    texture.subresource = tex_builder.image_view_info.subresourceRange;
    
    VmaAllocationCreateInfo img_info = {};
    img_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    img_info.flags = (
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT |
        VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT
    );

    // Create a "backing image", with the maximum allowed width and height of the texture
    VkImage backing_image = VK_NULL_HANDLE;
    VkImageCreateInfo backing_info = tex_builder.image_info;
    backing_info.extent.width = 8192;
    backing_info.extent.height = 8192;
    
    VkResult result = vkCreateImage(context.device, &backing_info, NULL, &backing_image);
    VK_CHECK(result, "failed to create backing image");

    result = vmaAllocateMemoryForImage(
        allocator.allocator, 
        backing_image, 
        &img_info, 
        &texture.allocation, 
        &texture.allocation_info
    );
    VK_CHECK(result, "failed to allocate memory for texture");
    
    // Destroy the backing image since we no longer need it, create the actual image and bind it
    vkDestroyImage(context.device, backing_image, NULL);
    result = vkCreateImage(context.device, &tex_builder.image_info, NULL, &texture.image);
    VK_CHECK(result, "failed to create image");

    result = vmaBindImageMemory(allocator.allocator, texture.allocation, texture.image);
    VK_CHECK(result, "failed to bind image memory");

    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    MdCommandEncoder encoder = {};
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (p_command_encoder == NULL)
    {
        result = mdCreateCommandEncoder(
            context, 
            allocator.queue.queue_index, 
            encoder,
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        );
        VK_CHECK(result, "failed to create command encoder");

        result = mdAllocateCommandBuffers(context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, encoder);
        VK_CHECK(result, "failed to allocate command buffers");
        cmd_buffer = encoder.buffers[0];
    
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.pInheritanceInfo = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buffer, &begin_info);
    }
    else cmd_buffer = p_command_encoder->buffers[command_buffer_index];
    
    mdTransitionImageLayout(
        texture, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        cmd_buffer
    );
    
    if (p_command_encoder == NULL)
    {
        vkEndCommandBuffer(cmd_buffer);

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer;

        vkQueueSubmit(allocator.queue.queue_handle, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(allocator.queue.queue_handle);

        mdDestroyCommandEncoder(context, encoder);    
    }
    
    return result;
}

VkResult mdResizeAttachmentTexture( MdRenderContext &context, 
                                    u32 w, 
                                    u32 h,
                                    MdGPUTextureBuilder &tex_builder,
                                    MdGPUAllocator &allocator,
                                    MdGPUTexture &texture)
{
    if (w > MAX_ATTACHMENT_WIDTH || h > MAX_ATTACHMENT_HEIGHT)
    {
        LOG_ERROR(
            "width (%d) or height (%d) is greater than the maximum allowed width (%d) or height (%d)\n",
            w, h, MAX_ATTACHMENT_WIDTH, MAX_ATTACHMENT_HEIGHT
        );
        return VK_ERROR_UNKNOWN;
    }

    if (texture.sampler != VK_NULL_HANDLE)
        vkDestroySampler(context.device, texture.sampler, NULL);
    if (texture.image_view != VK_NULL_HANDLE)
        vkDestroyImageView(context.device, texture.image_view, NULL);
    if (texture.image != VK_NULL_HANDLE)
        vkDestroyImage(context.device, texture.image, NULL);

    tex_builder.image_info.extent.width = w;
    tex_builder.image_info.extent.height = h;

    VkResult result = vkCreateImage(context.device, &tex_builder.image_info, NULL, &texture.image);
    VK_CHECK(result, "failed to create image");

    result = vmaBindImageMemory(allocator.allocator, texture.allocation, texture.image);
    VK_CHECK(result, "failed to bind image memory");

    tex_builder.image_view_info.image = texture.image;
    result = vkCreateImageView(context.device, &tex_builder.image_view_info, NULL, &texture.image_view);
    VK_CHECK(result, "failed to create texture image view");

    result = vkCreateSampler(context.device, &tex_builder.sampler_info, NULL, &texture.sampler);
    VK_CHECK(result, "failed to create texture image sampler");

    return result;
}

void mdSetTextureUsage(MdGPUTextureBuilder &builder, VkImageUsageFlags usage, VkImageAspectFlagBits image_aspect)
{
    builder.image_view_info.subresourceRange.aspectMask = image_aspect;
    builder.image_info.usage = usage;
}

void mdSetFilterWrap(   MdGPUTextureBuilder &builder, 
                        VkSamplerAddressMode mode_u, 
                        VkSamplerAddressMode mode_v, 
                        VkSamplerAddressMode mode_w)
{
    builder.sampler_info.addressModeU = mode_u;
    builder.sampler_info.addressModeV = mode_v;
    builder.sampler_info.addressModeW = mode_w;
}

void mdSetTextureBorderColor(MdGPUTextureBuilder &builder, VkBorderColor color)
{
    builder.sampler_info.borderColor = color;
}

void mdSetMipmapOptions(MdGPUTextureBuilder &builder, VkSamplerMipmapMode mode)
{
    builder.sampler_info.mipmapMode = mode;
}

void mdSetMagFilters(MdGPUTextureBuilder &builder, VkFilter mag_filter, VkFilter min_filter)
{
    builder.sampler_info.magFilter = mag_filter;
    builder.sampler_info.minFilter = min_filter;
}

void mdDestroyTexture(MdGPUAllocator &allocator, MdGPUTexture &texture)
{
    if (texture.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(allocator.device, texture.sampler, NULL);
        texture.sampler = VK_NULL_HANDLE;
    }

    if (texture.image_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(allocator.device, texture.image_view, NULL);
        texture.image_view = VK_NULL_HANDLE;
    }
    
    vmaDestroyImage(allocator.allocator, texture.image, texture.allocation);
}

void mdDestroyAttachmentTexture(MdGPUAllocator &allocator, MdGPUTexture &texture)
{
    if (texture.sampler != VK_NULL_HANDLE)
        vkDestroySampler(allocator.device, texture.sampler, NULL);
    if (texture.image_view != VK_NULL_HANDLE)
        vkDestroyImageView(allocator.device, texture.image_view, NULL);
    if (texture.image != VK_NULL_HANDLE)
        vkDestroyImage(allocator.device, texture.image, NULL);

    vmaFreeMemory(allocator.allocator, texture.allocation);
}

void mdDestroyGPUAllocator(MdGPUAllocator &allocator)
{
    vmaDestroyBuffer(
        allocator.allocator, 
        allocator.staging_buffer.buffer, 
        allocator.staging_buffer.allocation
    );
    vmaDestroyAllocator(allocator.allocator);
}

#pragma endregion

#pragma region [ Render Pass ]
void mdCreateRenderTargetBuilder(u16 w, u16 h, MdRenderTargetBuilder &target)
{
    target.w = w;
    target.h = h;
}

void mdRenderTargetAddColorAttachment(  MdRenderTargetBuilder &builder, 
                                        VkFormat image_format, 
                                        VkImageLayout initial_layout, 
                                        VkImageLayout final_layout)
{
    VkAttachmentDescription color_att = {};
    VkAttachmentReference color_ref = {};
    VkSubpassDependency color_deps = {};
    {
        color_att.format = image_format;
        color_att.samples = VK_SAMPLE_COUNT_1_BIT;
        color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_att.initialLayout = initial_layout;
        color_att.finalLayout = final_layout;

        color_ref.attachment = builder.descriptions.size();
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
        color_deps.srcSubpass = VK_SUBPASS_EXTERNAL;
        color_deps.dstSubpass = 0;
        color_deps.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        color_deps.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        color_deps.srcAccessMask = 0;
        color_deps.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    builder.descriptions.push_back(color_att);
    builder.color_references.push_back(color_ref);
    builder.subpass_dependencies.push_back(color_deps);
}

void mdRenderTargetAddDepthAttachment(  MdRenderTargetBuilder &builder, 
                                        VkFormat image_format, 
                                        VkImageLayout initial_layout, 
                                        VkImageLayout final_layout)
{
    VkAttachmentDescription depth_att = {};
    VkAttachmentReference depth_ref = {};
    VkSubpassDependency depth_deps = {};
    {
        depth_att.format = image_format;
        depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.initialLayout = initial_layout;
        depth_att.finalLayout = final_layout;

        depth_ref.attachment = builder.descriptions.size();
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
        depth_deps.srcSubpass = VK_SUBPASS_EXTERNAL;
        depth_deps.dstSubpass = 0;
        depth_deps.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depth_deps.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depth_deps.srcAccessMask = 0;
        depth_deps.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    builder.descriptions.push_back(depth_att);
    builder.depth_references.push_back(depth_ref);
    builder.subpass_dependencies.push_back(depth_deps);
}

VkResult mdBuildRenderTarget(MdRenderContext &context, MdRenderTargetBuilder &builder, MdRenderTarget &target)
{
    target.w = builder.w;
    target.h = builder.h;
    
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.colorAttachmentCount = builder.color_references.size();
    subpass.pColorAttachments = builder.color_references.data();
    subpass.pDepthStencilAttachment = builder.depth_references.data();
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = NULL;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = NULL;
    subpass.pResolveAttachments = NULL;

    VkRenderPassCreateInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = builder.subpass_dependencies.size();
    rp_info.pDependencies = builder.subpass_dependencies.data();
    rp_info.attachmentCount = builder.descriptions.size();
    rp_info.pAttachments = builder.descriptions.data();
    rp_info.flags = 0;

    VkResult result = vkCreateRenderPass(context.device, &rp_info, NULL, &target.pass);
    VK_CHECK(result, "failed to create renderpass");

    return result;
}

VkResult mdRenderTargetAddFramebuffer(MdRenderContext &context, MdRenderTarget &target, const std::vector<VkImageView> &views)
{
    VkFramebufferCreateInfo fb_info {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.attachmentCount = 1;
    fb_info.width = target.w;
    fb_info.height = target.h;
    fb_info.layers = 1;
    fb_info.renderPass = target.pass;
    fb_info.attachmentCount = views.size();
    fb_info.pAttachments = views.data();

    VkFramebuffer handle = VK_NULL_HANDLE;

    VkResult result = vkCreateFramebuffer(context.device, &fb_info, NULL, &handle);
    VK_CHECK(result, "failed to create framebuffer");

    target.buffers.push_back(handle);
    return result;
}

void mdDestroyRenderTarget(MdRenderContext &context, MdRenderTarget &target)
{
    if (target.buffers.size() > 0)
    {
        for (u32 i=0; i<target.buffers.size(); i++)
            vkDestroyFramebuffer(context.device, target.buffers[i], NULL);
            
        target.buffers.clear();
    }
    vkDestroyRenderPass(context.device, target.pass, NULL);
}
#pragma endregion

#pragma region [ Shader Modules and Descriptors ]
VkResult mdLoadShaderSPIRV( MdRenderContext &context, 
                            u32 code_size, 
                            const u32 *p_code, 
                            VkShaderStageFlagBits stage,
                            MdShaderSource &source)
{
    VkPipelineShaderStageCreateInfo stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage_info.pSpecializationInfo = NULL; // TO-DO: worry about this later
    stage_info.pName = "main";
    stage_info.flags = 0;
    stage_info.stage = stage;

    VkShaderModuleCreateInfo module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = code_size;
    module_info.pCode = p_code;
    module_info.flags = 0;
    VkResult result = vkCreateShaderModule(context.device, &module_info, NULL, &stage_info.module);
    VK_CHECK(result, "failed to create shader module");

    source.modules.push_back(stage_info);
    return result;
}

void mdShaderAddBinding(    MdShaderSource &source,
                            u32 binding_index, 
                            VkDescriptorType type, 
                            u32 count, 
                            VkShaderStageFlags stage_flags, 
                            const VkSampler* p_immutable_samplers)
{
    source.bindings.push_back({
        .binding = binding_index,
        .descriptorType = type,
        .descriptorCount = count,
        .stageFlags = stage_flags,
        .pImmutableSamplers = p_immutable_samplers
    });
}

void mdDestroyShaderSource(MdRenderContext &context, MdShaderSource &source)
{
    if (source.modules.size() > 0)
    {
        for (u32 i=0; i<source.modules.size(); i++)
        {
            if (source.modules[i].module != VK_NULL_HANDLE)
                vkDestroyShaderModule(context.device, source.modules[i].module, NULL);
        }
    }
}

void mdCreateDescriptorAllocator(u32 max_sets, u32 max_bindings, MdDescriptorSetAllocator &allocator)
{
    allocator.max_sets = max_sets;
    allocator.max_bindings = max_bindings;
    allocator.sets.reserve(max_sets);
    allocator.bindings.reserve(max_bindings);
}

void mdAddDescriptorBinding(u32 count, 
                            VkDescriptorType type, 
                            VkShaderStageFlagBits stage, 
                            const VkSampler *p_samplers,
                            MdDescriptorSetAllocator &allocator)
{
    VkDescriptorSetLayoutBinding binding = {};
    binding.descriptorCount = count;
    binding.descriptorType = type;
    binding.stageFlags = stage;
    binding.pImmutableSamplers = p_samplers;
    binding.binding = allocator.bindings.size();

    allocator.bindings.push_back(binding);
}

void mdUpdateDescriptorSetImage(MdRenderContext &context,
                                MdDescriptorSetAllocator &allocator, 
                                u32 binding_index,
                                u32 set_index,
                                MdGPUTexture &texture)
{
    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.sampler = texture.sampler;
    image_info.imageView = texture.image_view;
    
    VkWriteDescriptorSet write_set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_set.pBufferInfo = NULL;
    write_set.pImageInfo = &image_info;
    write_set.pTexelBufferView = NULL;
    write_set.dstBinding = binding_index;
    write_set.dstSet = allocator.sets[set_index];

    vkUpdateDescriptorSets(
        context.device, 
        1, 
        &write_set, 
        0, 
        NULL
    );
}

void mdUpdateDescriptorSetUBO(  MdRenderContext &context,
                                MdDescriptorSetAllocator &allocator, 
                                u32 binding_index,
                                u32 set_index,
                                MdGPUBuffer &buffer, 
                                u32 offset, 
                                u32 range)
{
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = buffer.buffer;
    buffer_info.offset = offset;
    buffer_info.range = range;

    VkWriteDescriptorSet write_set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write_set.pBufferInfo = &buffer_info;
    write_set.pImageInfo = NULL;
    write_set.pTexelBufferView = NULL;
    write_set.dstBinding = binding_index;
    write_set.dstSet = allocator.sets[set_index];

    vkUpdateDescriptorSets(
        context.device, 
        1, 
        &write_set, 
        0, 
        NULL
    );
}

VkResult mdCreateDescriptorSets(u32 set_count, MdRenderContext &context, MdDescriptorSetAllocator &allocator)
{
    VkResult result = VK_ERROR_UNKNOWN;
    if (allocator.layout == VK_NULL_HANDLE)
    {
        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = allocator.bindings.size();
        layout_info.pBindings = allocator.bindings.data();
        layout_info.flags = 0;
        
        result = vkCreateDescriptorSetLayout(context.device, &layout_info, NULL, &allocator.layout);
        VK_CHECK(result, "failed to create descriptor set layout");
    }

    if (allocator.pool == VK_NULL_HANDLE)
    {
        std::vector<VkDescriptorPoolSize> pool_sizes;
        pool_sizes.reserve(allocator.bindings.size());
        for (u32 i=0; i<allocator.bindings.size(); i++)
        {
            pool_sizes.push_back({
                allocator.bindings[i].descriptorType, 
                allocator.bindings[i].descriptorCount
            });
        }

        VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.poolSizeCount = pool_sizes.size();
        pool_info.pPoolSizes = pool_sizes.data();
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = allocator.max_sets;
        
        result = vkCreateDescriptorPool(context.device, &pool_info, NULL, &allocator.pool);
        pool_sizes.clear();
        VK_CHECK(result, "failed to create descriptor pool");
    }

    if (allocator.sets.size()+set_count > allocator.max_sets)
    {
        LOG_ERROR("set count (%ld + %d) must not be greater than the max sets (%d)", 
            allocator.sets.size(), 
            set_count, 
            allocator.max_sets
        );
        return VK_ERROR_UNKNOWN;
    }

    u32 end_index = allocator.sets.size()+set_count;
    for (u32 i=allocator.sets.size();i<end_index;i++)
        allocator.sets.push_back(VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = allocator.pool;
    alloc_info.descriptorSetCount = set_count;
    alloc_info.pSetLayouts = &allocator.layout;
    result = vkAllocateDescriptorSets(context.device, &alloc_info, allocator.sets.data());
    VK_CHECK(result, "failed to allocate descriptor sets");
    return result;
}

void mdDestroyDescriptorSetAllocator(MdRenderContext &context, MdDescriptorSetAllocator &allocator)
{
    if (allocator.sets.size() > 0)
        vkFreeDescriptorSets(context.device, allocator.pool, allocator.sets.size(), allocator.sets.data());

    allocator.sets.clear();
    allocator.bindings.clear();

    if (allocator.pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(context.device, allocator.pool, NULL);

    if (allocator.layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(context.device, allocator.layout, NULL);
}

#pragma endregion

#pragma region [ Geometry Input State ]
VkFormat mdGeometryInputCalculateVertexFormat(MdVertexComponentType type, u32 count)
{
    const VkFormat format_matrix[] = {
        VK_FORMAT_R32_SFLOAT,VK_FORMAT_R64_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R16_UINT,VK_FORMAT_R8_UINT,
        VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R64G64_UINT,VK_FORMAT_R32G32_UINT,VK_FORMAT_R16G16_UINT,VK_FORMAT_R8G8_UINT,
        VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R64G64B64_UINT,VK_FORMAT_R32G32B32_UINT,VK_FORMAT_R16G16B16_UINT,VK_FORMAT_R8G8B8_UINT,
        VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R64G64B64A64_UINT,VK_FORMAT_R32G32B32A32_UINT,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R8G8B8A8_UINT
    };

    return format_matrix[count*MD_VERTEX_COMPONENT_TYPE_COUNT + type];
}

void mdInitGeometryInputState(MdPipelineGeometryInputState &stage)
{
    stage.vertex_info.flags = 0;
    stage.vertex_info.vertexAttributeDescriptionCount = 0;
    stage.vertex_info.pVertexAttributeDescriptions = NULL;
    stage.vertex_info.vertexBindingDescriptionCount = 0;
    stage.vertex_info.pVertexBindingDescriptions = NULL;
    
    stage.assembly_info.flags = 0;
    stage.assembly_info.primitiveRestartEnable = VK_FALSE;
    stage.assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

void mdGeometryInputAddVertexBinding(   MdPipelineGeometryInputState &stage, 
                                        VkVertexInputRate rate, 
                                        u32 stride)
{
    VkVertexInputBindingDescription binding = {};
    binding.binding = stage.bindings.size();
    binding.inputRate = rate;
    binding.stride = stride;

    stage.bindings.push_back(binding);
}

void mdGeometryInputAddAttribute(   MdPipelineGeometryInputState &stage, 
                                    u32 binding, 
                                    u32 location, 
                                    u32 count, 
                                    MdVertexComponentType type, 
                                    u32 offset)
{
    if (binding + 1 > stage.bindings.size())
    {
        LOG_ERROR("binding index (%d) must not exceed current number of bindings (%d)\n", binding, stage.bindings.size());
        return;
    }

    u32 index = 0;
    bool found = false;
    for (u32 i=0; i<stage.attributes.size(); i++)
    {
        if (!(stage.attributes[i].binding == binding && stage.attributes[i].location == location))
            continue;

        index = i;
        break;
    }

    if (!found)
    {
        index = stage.attributes.size();
        stage.attributes.push_back({});
    }

    stage.attributes[index].binding = binding;
    stage.attributes[index].location = location;
    stage.attributes[index].format = mdGeometryInputCalculateVertexFormat(type, count);
    stage.attributes[index].offset = offset;
}
 
void MdGeometryInputStageSetTopology(   MdPipelineGeometryInputState &stage, 
                                        VkPrimitiveTopology topology, 
                                        VkBool32 primitive_restart)
{
    stage.assembly_info.topology = topology;
    stage.assembly_info.primitiveRestartEnable = primitive_restart;
}

void mdBuildGeometryInputState(MdPipelineGeometryInputState &stage)
{
    stage.vertex_info.vertexAttributeDescriptionCount = stage.attributes.size();
    stage.vertex_info.pVertexAttributeDescriptions = stage.attributes.data();
    stage.vertex_info.vertexBindingDescriptionCount = stage.bindings.size();
    stage.vertex_info.pVertexBindingDescriptions = stage.bindings.data();
}

void mdBuildDefaultGeometryInputState(MdPipelineGeometryInputState &stage)
{
    mdInitGeometryInputState(stage);
    mdGeometryInputAddVertexBinding(stage, VK_VERTEX_INPUT_RATE_VERTEX, 8*sizeof(f32));
    mdGeometryInputAddAttribute(stage, 0, 0, 3, MD_F32, 0);
    mdGeometryInputAddAttribute(stage, 0, 1, 3, MD_F32, 3*sizeof(f32));
    mdGeometryInputAddAttribute(stage, 0, 2, 2, MD_F32, 6*sizeof(f32));
    mdBuildGeometryInputState(stage);
}
#pragma endregion

#pragma region [ Rasterization State ]
void mdInitRasterizationState(MdPipelineRasterizationState &stage)
{
    stage.raster_info.flags = 0;
    stage.raster_info.rasterizerDiscardEnable = VK_FALSE;
    stage.raster_info.depthBiasEnable = VK_FALSE;
    stage.raster_info.depthBiasConstantFactor = 1.0f;
    stage.raster_info.depthBiasSlopeFactor = 0.0f;
    stage.raster_info.depthClampEnable = VK_FALSE;
    stage.raster_info.depthBiasClamp = 0.0f;
    stage.raster_info.cullMode = VK_CULL_MODE_NONE;
    stage.raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    stage.raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    stage.raster_info.lineWidth = 1.0f;
}

void mdRasterizationStateEnableDepthBias(   MdPipelineRasterizationState &stage,
                                            VkBool32 enable,
                                            f32 constant_factor,
                                            f32 slope_factor)
{
    stage.raster_info.depthBiasEnable = enable;
    stage.raster_info.depthBiasConstantFactor = constant_factor;
    stage.raster_info.depthBiasSlopeFactor = slope_factor;
}

void mdRasterizationStateEnableDepthClamp(  MdPipelineRasterizationState &stage,
                                            VkBool32 enable,
                                            f32 clamp)
{
    stage.raster_info.depthClampEnable = enable;
    stage.raster_info.depthBiasClamp = clamp;
}

void mdRasterizationStateSetCullMode(   MdPipelineRasterizationState &stage, 
                                        VkCullModeFlags mode, 
                                        VkFrontFace face)
{
    stage.raster_info.cullMode = mode;
    stage.raster_info.frontFace = face;
}

void mdRasterizationStateSetPolygonMode(MdPipelineRasterizationState &stage,
                                        VkPolygonMode mode)
{
    stage.raster_info.polygonMode = mode;
}

void mdBuildRasterizationState(MdPipelineRasterizationState &stage){}
void mdBuildDefaultRasterizationState(MdPipelineRasterizationState &stage)
{
    mdInitRasterizationState(stage);
}
#pragma endregion

#pragma region [ Color Blend State ]
void mdInitColorBlendState(MdPipelineColorBlendState &stage)
{
    stage.color_blend_info.flags = 0;
    stage.color_blend_info.attachmentCount = 0;
    stage.color_blend_info.pAttachments = NULL;
    stage.color_blend_info.logicOpEnable = VK_FALSE;
    stage.color_blend_info.logicOp = VK_LOGIC_OP_COPY;
    
    stage.color_blend_info.blendConstants[0] = 0.0f;
    stage.color_blend_info.blendConstants[1] = 0.0f;
    stage.color_blend_info.blendConstants[2] = 0.0f;
    stage.color_blend_info.blendConstants[3] = 0.0f;
}

void mdColorBlendStateLogicOpEnable(MdPipelineColorBlendState &stage, 
                                    VkBool32 enable, 
                                    f32 c0, 
                                    f32 c1, 
                                    f32 c2, 
                                    f32 c3)
{
    stage.color_blend_info.logicOpEnable = enable;
    stage.color_blend_info.blendConstants[0] = c0;
    stage.color_blend_info.blendConstants[1] = c1;
    stage.color_blend_info.blendConstants[2] = c2;
    stage.color_blend_info.blendConstants[3] = c3;
}

void mdColorBlendStateAddAttachment(MdPipelineColorBlendState &stage, VkBool32 blend_enable)
{
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = blend_enable;
    stage.attachment.push_back(attachment);
}

void mdColorBlendAttachmentBlendEnable(MdPipelineColorBlendState &stage, u32 index, VkBool32 enable)
{
    stage.attachment[index].blendEnable = enable;
}

void mdColorBlendAttachmentSetColorBlendOp( MdPipelineColorBlendState &stage, 
                                            u32 index, 
                                            VkBlendOp op,
                                            VkBlendFactor src_factor,
                                            VkBlendFactor dst_factor)
{
    stage.attachment[index].colorBlendOp = op;
    stage.attachment[index].srcColorBlendFactor = src_factor;
    stage.attachment[index].dstColorBlendFactor = dst_factor;
}

void mdColorBlendAttachmentSetAlphaBlendOp( MdPipelineColorBlendState &stage, 
                                            u32 index, 
                                            VkBlendOp op,
                                            VkBlendFactor src_factor,
                                            VkBlendFactor dst_factor)
{
    stage.attachment[index].alphaBlendOp = op;
    stage.attachment[index].srcAlphaBlendFactor = src_factor;
    stage.attachment[index].dstAlphaBlendFactor = dst_factor;
}

void mdColorBlendAttachmentSetColorWriteMask(   MdPipelineColorBlendState &stage, 
                                                u32 index, 
                                                VkColorComponentFlags mask)
{
    stage.attachment[index].colorWriteMask = mask;
}

void mdBuildColorBlendState(MdPipelineColorBlendState &stage)
{
    stage.color_blend_info.attachmentCount = stage.attachment.size();
    stage.color_blend_info.pAttachments = stage.attachment.data();
}

void mdBuildDefaultColorBlendState(MdPipelineColorBlendState &stage)
{
    mdInitColorBlendState(stage);
    stage.attachment.push_back({});
    stage.attachment[0].blendEnable = VK_FALSE;
    stage.attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
    stage.attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    stage.attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    stage.attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
    stage.attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    stage.attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    stage.attachment[0].colorWriteMask = (
        VK_COLOR_COMPONENT_R_BIT | 
        VK_COLOR_COMPONENT_G_BIT | 
        VK_COLOR_COMPONENT_B_BIT | 
        VK_COLOR_COMPONENT_A_BIT
    );
    stage.color_blend_info.attachmentCount = 1;
    stage.color_blend_info.pAttachments = stage.attachment.data();
}
#pragma endregion

#pragma region [ Pipeline ]
VkResult mdCreateGraphicsPipeline(  MdRenderContext &context, 
                                    MdShaderSource &shaders, 
                                    usize desc_count,
                                    MdDescriptorSetAllocator *p_descriptor_sets,
                                    MdPipelineGeometryInputState *p_geometry_state,
                                    MdPipelineRasterizationState *p_raster_state,
                                    MdPipelineColorBlendState *p_color_blend_state,
                                    MdRenderTarget &target,
                                    u32 pipeline_count, 
                                    MdPipelineState &pipeline)
{
    VkResult result;

    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.flags = 0;
    std::vector<VkDescriptorSetLayout> layouts;
    
    if (p_descriptor_sets != NULL)
    {
        layout_info.setLayoutCount = desc_count;
        layouts.reserve(desc_count);
        
        for (usize i=0; i<desc_count; i++)
            layouts.push_back(p_descriptor_sets[i].layout);
        
        layout_info.pSetLayouts = layouts.data();
    }
    else
    {
        layout_info.setLayoutCount = 0;
        layout_info.pSetLayouts = NULL;
    }
    
    layout_info.pushConstantRangeCount = 0;
    layout_info.pPushConstantRanges = NULL;

    result = vkCreatePipelineLayout(context.device, &layout_info, NULL, &pipeline.layout);
    VK_CHECK(result, "failed to create pipeline layout");

    VkDynamicState states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    {
        dynamic_info.dynamicStateCount = sizeof(states)/sizeof(VkDynamicState);
        dynamic_info.pDynamicStates = states;
    }

    VkPipelineViewportStateCreateInfo viewport_info = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    {
        viewport_info.viewportCount = 1;
        viewport_info.scissorCount = 1;
        viewport_info.flags = 0;
    }

    VkPipelineMultisampleStateCreateInfo multisample_info = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    {
        multisample_info.alphaToCoverageEnable = VK_FALSE;
        multisample_info.alphaToOneEnable = VK_FALSE;
        multisample_info.sampleShadingEnable = VK_FALSE;
        multisample_info.pSampleMask = NULL;
        multisample_info.minSampleShading = 1.0f;
        multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisample_info.flags = 0;
    }

    VkPipelineDepthStencilStateCreateInfo depth_info = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_info.depthWriteEnable = VK_TRUE;
    depth_info.depthTestEnable = VK_TRUE;
    depth_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_info.depthBoundsTestEnable = VK_FALSE;
    depth_info.minDepthBounds = 0.0f;
    depth_info.maxDepthBounds = 1.0f;
    depth_info.stencilTestEnable = VK_FALSE;
    depth_info.front = {};
    depth_info.back = {};

    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.flags = 0;
    pipeline_info.basePipelineIndex = -1;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.layout = pipeline.layout;
    pipeline_info.renderPass = target.pass;
    pipeline_info.subpass = 0;

    pipeline_info.stageCount = shaders.modules.size();
    pipeline_info.pStages = shaders.modules.data();

    pipeline_info.pDynamicState = &dynamic_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pVertexInputState = &p_geometry_state->vertex_info;
    pipeline_info.pInputAssemblyState = &p_geometry_state->assembly_info;
    pipeline_info.pRasterizationState = &p_raster_state->raster_info;
    pipeline_info.pColorBlendState = &p_color_blend_state->color_blend_info;
    pipeline_info.pDepthStencilState = &depth_info;
    pipeline_info.pMultisampleState = &multisample_info;
    pipeline_info.pTessellationState = NULL;

    pipeline.pipeline.reserve(pipeline_count);
    for (u32 i=0; i<pipeline_count; i++)
        pipeline.pipeline.push_back(VK_NULL_HANDLE);
    
    result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, pipeline.pipeline.data());
    VK_CHECK(result, "failed to create graphics pipeline");
    return result;
}

void mdDestroyPipelineState(MdRenderContext &context, MdPipelineState &pipeline)
{
    if (pipeline.pipeline.size() > 0)
    {
        for (auto p : pipeline.pipeline)
            if (p != VK_NULL_HANDLE)
                vkDestroyPipeline(context.device, p, NULL);
        
        if (pipeline.layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(context.device, pipeline.layout, NULL);
    }
}

#pragma endregion
