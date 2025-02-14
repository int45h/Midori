#pragma once
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <typedefs.h>
#include <vma/vma_usage.h>

#pragma region [ Render Context ]
struct MdGPUTexture
{
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
    VkFormat format;
    VkImage image;
    VkImageView image_view;
    VkSampler sampler;
    VkImageSubresourceRange subresource;
    u16 w, h;
    u16 channels;
};

struct MdRenderContext
{
    u32 api_version;
    vkb::Instance instance;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::Device device;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    vkb::Swapchain swapchain;

    std::vector<VkImageView> sw_image_views;
    std::vector<VkImage> sw_images;
};

struct MdRenderQueue
{
    VkQueue queue_handle;
    i32 queue_index;

    MdRenderQueue() : queue_handle(VK_NULL_HANDLE), queue_index(-1) {}
    MdRenderQueue(VkQueue handle, i32 index) : queue_handle(handle), queue_index(index) {}
};

MdResult mdInitContext(MdRenderContext &context, const std::vector<const char*> &instance_extensions);
void mdDestroyContext(MdRenderContext &context);
MdResult mdCreateDevice(MdRenderContext &context);
MdResult mdGetQueue(VkQueueFlagBits queue_type, MdRenderContext &context, MdRenderQueue &queue);
MdResult mdGetSwapchain(MdRenderContext &context, bool rebuild = false);
#pragma endregion

#pragma region [ Command Encoder ]
struct MdCommandEncoder
{
    std::vector<VkCommandBuffer> buffers;
    VkCommandPool pool;
};

VkResult mdCreateCommandEncoder(MdRenderContext &context, u32 queue_family_index, MdCommandEncoder &encoder, VkCommandPoolCreateFlags flags = 0);
VkResult mdAllocateCommandBuffers(MdRenderContext &context, u32 buffer_count, VkCommandBufferLevel level, MdCommandEncoder &encoder);
void mdDestroyCommandEncoder(MdRenderContext &context, MdCommandEncoder &encoder);

#pragma endregion

#pragma region [ Memory ]

struct MdGPUBuffer
{
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
    VkBuffer buffer;
    u32 size;
};

struct MdGPUAllocator
{
    VkDevice device;
    VmaAllocator allocator;
    MdGPUBuffer staging_buffer;
    MdRenderQueue queue;
};

VkResult mdCreateGPUAllocator(MdRenderContext &context, MdGPUAllocator &allocator, MdRenderQueue queue, VkDeviceSize staging_buffer_size = 1024*1024);
VkResult mdAllocateGPUBuffer(VkBufferUsageFlags usage, u32 size, MdGPUAllocator &allocator, MdGPUBuffer &buffer);
VkResult mdAllocateGPUUniformBuffer(u32 size, MdGPUAllocator &allocator, MdGPUBuffer &buffer);
void mdFreeGPUBuffer(MdGPUAllocator &allocator, MdGPUBuffer &buffer);
void mdFreeUniformBuffer(MdGPUAllocator &allocator, MdGPUBuffer &buffer);
VkResult mdUploadToGPUBuffer(   MdRenderContext &context, 
                                MdGPUAllocator &allocator, 
                                u32 offset, 
                                u32 range, 
                                const void *p_data, 
                                MdGPUBuffer &buffer, 
                                MdCommandEncoder *p_command_encoder = NULL,
                                u32 command_buffer_index = 0);
VkResult mdUploadToUniformBuffer(   MdRenderContext &context, 
                                    MdGPUAllocator &allocator, 
                                    u32 offset, 
                                    u32 range, 
                                    const void *p_data, 
                                    MdGPUBuffer &buffer);
#pragma endregion

#pragma region [ Textures (Related to memory) ]

u64 mdGetTextureSize(MdGPUTexture &texture);
void mdTransitionImageLayout(   MdGPUTexture &texture, 
                                VkImageLayout src_layout, 
                                VkImageLayout dst_layout, 
                                VkAccessFlags src_access,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags src_stage,
                                VkPipelineStageFlags dst_stage,
                                VkCommandBuffer buffer);
void mdTransitionImageLayoutWaitEvent(  MdGPUTexture &texture, 
                                        VkImageLayout src_layout, 
                                        VkImageLayout dst_layout, 
                                        VkAccessFlags src_access, 
                                        VkAccessFlags dst_access, 
                                        VkPipelineStageFlags src_stage, 
                                        VkPipelineStageFlags dst_stage, 
                                        VkCommandBuffer buffer,
                                        VkEvent *p_event);
                                        
struct MdGPUTextureBuilder
{
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkImageViewCreateInfo image_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    u16 channels;
};

void mdCreateTextureBuilder2D(MdGPUTextureBuilder &builder, u16 w, u16 h, VkFormat format, VkImageAspectFlags aspect, u16 pitch = 4);
VkResult mdBuildTexture2D(  MdRenderContext &context, 
                            MdGPUTextureBuilder &tex_builder,
                            MdGPUAllocator &allocator,
                            MdGPUTexture &texture, 
                            const void *data,
                            MdCommandEncoder *p_command_encoder = NULL,
                            u32 command_buffer_index = 0);

#define MAX_ATTACHMENT_WIDTH 8192
#define MAX_ATTACHMENT_HEIGHT 8192

VkResult mdBuildDepthAttachmentTexture2D( MdRenderContext &context, 
                                MdGPUTextureBuilder &tex_builder,
                                MdGPUAllocator &allocator,
                                MdGPUTexture &texture, 
                                MdCommandEncoder *p_command_encoder = NULL,
                                u32 command_buffer_index = 0);
VkResult mdBuildColorAttachmentTexture2D(   MdRenderContext &context, 
                                            MdGPUTextureBuilder &tex_builder,
                                            MdGPUAllocator &allocator,
                                            MdGPUTexture &texture, 
                                            MdCommandEncoder *p_command_encoder = NULL,
                                            u32 command_buffer_index = 0);
VkResult mdResizeAttachmentTexture( MdRenderContext &context, 
                                    u32 w, 
                                    u32 h,
                                    MdGPUTextureBuilder &tex_builder,
                                    MdGPUAllocator &allocator,
                                    MdGPUTexture &texture);
void mdSetTextureUsage(MdGPUTextureBuilder &builder, VkImageUsageFlags usage, VkImageAspectFlagBits image_aspect);
void mdSetFilterWrap(   MdGPUTextureBuilder &builder, 
                        VkSamplerAddressMode mode_u, 
                        VkSamplerAddressMode mode_v, 
                        VkSamplerAddressMode mode_w);
void mdSetTextureBorderColor(MdGPUTextureBuilder &builder, VkBorderColor color);
void mdSetMipmapOptions(MdGPUTextureBuilder &builder, VkSamplerMipmapMode mode);
void mdSetMagFilters(MdGPUTextureBuilder &builder, VkFilter mag_filter, VkFilter min_filter);
void mdDestroyTexture(MdGPUAllocator &allocator, MdGPUTexture &texture);
void mdDestroyAttachmentTexture(MdGPUAllocator &allocator, MdGPUTexture &texture);
void mdDestroyGPUAllocator(MdGPUAllocator &allocator);
#pragma endregion

#pragma region [ Render Pass ]
struct MdRenderTargetBuilder
{
    std::vector<VkAttachmentReference> color_references;
    std::vector<VkAttachmentReference> depth_references;
    std::vector<VkAttachmentDescription> descriptions;
    std::vector<VkSubpassDependency> subpass_dependencies;

    std::vector<VkImageView> color_views;
    std::vector<VkImageView> depth_views;
    
    u16 w, h;
};

struct MdRenderTarget
{
    VkRenderPass pass;
    std::vector<VkFramebuffer> buffers;

    u16 w, h;
};

void mdCreateRenderTargetBuilder(u16 w, u16 h, MdRenderTargetBuilder &target);
void mdRenderTargetAddColorAttachment(  MdRenderTargetBuilder &builder, 
                                        VkFormat image_format, 
                                        VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, 
                                        VkImageLayout final_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
void mdRenderTargetAddDepthAttachment(  MdRenderTargetBuilder &builder, 
                                        VkFormat image_format, 
                                        VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, 
                                        VkImageLayout final_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
VkResult mdBuildRenderTarget(MdRenderContext &context, MdRenderTargetBuilder &builder, MdRenderTarget &target);
VkResult mdRenderTargetAddFramebuffer(MdRenderContext &context, MdRenderTarget &target, const std::vector<VkImageView> &views);
void mdDestroyRenderTarget(MdRenderContext &context, MdRenderTarget &target);
#pragma endregion

#pragma region [ Shader Modules and Descriptors ]

struct MdShaderSource
{
    std::vector<VkPipelineShaderStageCreateInfo> modules;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

VkResult mdLoadShaderSPIRV( MdRenderContext &context, 
                            u32 code_size, 
                            const u32 *p_code, 
                            VkShaderStageFlagBits stage,
                            MdShaderSource &source);
void mdShaderAddBinding(    MdShaderSource &source,
                            u32 binding_index, 
                            VkDescriptorType type, 
                            u32 count, 
                            VkShaderStageFlags stage_flags, 
                            const VkSampler* p_immutable_samplers = NULL);
void mdDestroyShaderSource( MdRenderContext &context, MdShaderSource &source);

struct MdDescriptorSetAllocator
{
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorSet> sets; 

    u32 max_sets, max_bindings;
};

void mdCreateDescriptorAllocator(u32 max_sets, u32 max_bindings, MdDescriptorSetAllocator &allocator);
void mdAddDescriptorBinding(u32 count, 
                            VkDescriptorType type, 
                            VkShaderStageFlagBits stage, 
                            const VkSampler *p_samplers,
                            MdDescriptorSetAllocator &allocator);
void mdUpdateDescriptorSetImage(MdRenderContext &context,
                                MdDescriptorSetAllocator &allocator, 
                                u32 binding_index,
                                u32 set_index,
                                MdGPUTexture &texture);
void mdUpdateDescriptorSetUBO(  MdRenderContext &context,
                                MdDescriptorSetAllocator &allocator, 
                                u32 binding_index,
                                u32 set_index,
                                MdGPUBuffer &buffer, 
                                u32 offset, 
                                u32 range);
VkResult mdCreateDescriptorSets(u32 set_count, MdRenderContext &context, MdDescriptorSetAllocator &allocator);
void mdDestroyDescriptorSetAllocator(MdRenderContext &context, MdDescriptorSetAllocator &allocator);

#pragma endregion

#pragma region [ Geometry Input State ]
struct MdPipelineGeometryInputState
{
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::vector<VkVertexInputBindingDescription> bindings;

    VkPipelineVertexInputStateCreateInfo vertex_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly_info = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
};

enum MdVertexComponentType
{
    MD_F32 = 0,
    MD_U64,
    MD_U32,
    MD_U16,
    MD_U8,
    MD_VERTEX_COMPONENT_TYPE_COUNT
};

VkFormat mdGeometryInputCalculateVertexFormat(MdVertexComponentType type, u32 count);
void mdInitGeometryInputState(MdPipelineGeometryInputState &stage);
void mdGeometryInputAddVertexBinding(   MdPipelineGeometryInputState &stage, 
                                        VkVertexInputRate rate, 
                                        u32 stride);
void mdGeometryInputAddAttribute(   MdPipelineGeometryInputState &stage, 
                                    u32 binding, 
                                    u32 location, 
                                    u32 count, 
                                    MdVertexComponentType type, 
                                    u32 offset);
void MdGeometryInputStageSetTopology(   MdPipelineGeometryInputState &stage, 
                                        VkPrimitiveTopology topology, 
                                        VkBool32 primitive_restart = VK_FALSE);
void mdBuildGeometryInputState(MdPipelineGeometryInputState &stage);
void mdBuildDefaultGeometryInputState(MdPipelineGeometryInputState &stage);
#pragma endregion

#pragma region [ Rasterization State ]
struct MdPipelineRasterizationState
{
    VkPipelineRasterizationStateCreateInfo raster_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
};

void mdInitRasterizationState(MdPipelineRasterizationState &stage);
void mdRasterizationStateEnableDepthBias(   MdPipelineRasterizationState &stage,
                                            VkBool32 enable = VK_FALSE,
                                            f32 constant_factor = 1.0f,
                                            f32 slope_factor = 1.0f);
void mdRasterizationStateEnableDepthClamp(  MdPipelineRasterizationState &stage,
                                            VkBool32 enable = VK_FALSE,
                                            f32 clamp = 1.0f);
void mdRasterizationStateSetCullMode(   MdPipelineRasterizationState &stage, 
                                        VkCullModeFlags mode, 
                                        VkFrontFace face = VK_FRONT_FACE_COUNTER_CLOCKWISE);
void mdRasterizationStateSetPolygonMode(MdPipelineRasterizationState &stage,
                                        VkPolygonMode mode);
void mdBuildRasterizationState(MdPipelineRasterizationState &stage);
void mdBuildDefaultRasterizationState(MdPipelineRasterizationState &stage);
#pragma endregion

#pragma region [ Color Blend State ]
struct MdPipelineColorBlendState
{
    VkPipelineColorBlendStateCreateInfo color_blend_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    std::vector<VkPipelineColorBlendAttachmentState> attachment;
};

void mdInitColorBlendState(MdPipelineColorBlendState &stage);
void mdColorBlendStateLogicOpEnable(MdPipelineColorBlendState &stage, 
                                    VkBool32 enable, 
                                    f32 c0 = 0.0f, 
                                    f32 c1 = 0.0f, 
                                    f32 c2 = 0.0f, 
                                    f32 c3 = 0.0f);
void mdColorBlendStateAddAttachment(MdPipelineColorBlendState &stage, VkBool32 blend_enable = VK_FALSE);
void mdColorBlendAttachmentBlendEnable(MdPipelineColorBlendState &stage, u32 index, VkBool32 enable = VK_FALSE);
void mdColorBlendAttachmentSetColorBlendOp( MdPipelineColorBlendState &stage, 
                                            u32 index, 
                                            VkBlendOp op,
                                            VkBlendFactor src_factor = VK_BLEND_FACTOR_ONE,
                                            VkBlendFactor dst_factor = VK_BLEND_FACTOR_ZERO);
void mdColorBlendAttachmentSetAlphaBlendOp( MdPipelineColorBlendState &stage, 
                                            u32 index, 
                                            VkBlendOp op,
                                            VkBlendFactor src_factor = VK_BLEND_FACTOR_ONE,
                                            VkBlendFactor dst_factor = VK_BLEND_FACTOR_ZERO);
void mdColorBlendAttachmentSetColorWriteMask(   MdPipelineColorBlendState &stage, 
                                                u32 index, 
                                                VkColorComponentFlags mask);
void mdBuildColorBlendState(MdPipelineColorBlendState &stage);
void mdBuildDefaultColorBlendState(MdPipelineColorBlendState &stage);
#pragma endregion

#pragma region [ Pipeline ]

struct MdPipelineState
{
    VkPipelineLayout layout;
    std::vector<VkPipeline> pipeline;
};

VkResult mdCreateGraphicsPipeline(  MdRenderContext &context, 
                                    MdShaderSource &shaders, 
                                    usize desc_count,
                                    MdDescriptorSetAllocator *p_descriptor_sets,
                                    MdPipelineGeometryInputState *p_geometry_state,
                                    MdPipelineRasterizationState *p_raster_state,
                                    MdPipelineColorBlendState *p_color_blend_state,
                                    MdRenderTarget &target,
                                    u32 pipeline_count, 
                                    MdPipelineState &pipeline);
void mdDestroyPipelineState(MdRenderContext &context, MdPipelineState &pipeline);
#pragma endregion
