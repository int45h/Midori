#pragma once

#include "renderer_vk/renderer_vk_helpers.h"
#include <vulkan/vulkan_core.h>
#define MD_USE_VULKAN
#include <renderer_api.h>
#include <platform/window/window.h>
#include <simd_math.h>

#include <array>
typedef VkDescriptorSetLayoutBinding MdUniformBinding;
typedef u32 MdUniformSetHandle;
typedef u32 MdMaterialHandle;
typedef u32 MdPipelineHandle;

struct MdRenderContext;

struct MdCamera;
struct MdPipeline;
struct MdMaterial;

struct MdRenderer
{
    MdWindow window;
    MdRenderContext *context;
};

MdResult mdCreateRenderer(          u16 w, 
                                    u16 h, 
                                    const char *p_title, 
                                    MdRenderer &renderer);
void mdDestroyRenderer(             MdRenderer &renderer);

#pragma region [ Render Graph ]
enum MdRenderPassAttachmentType
{
    MD_ATTACHMENT_TYPE_COLOR,
    MD_ATTACHMENT_TYPE_DEPTH
};

struct MdRenderPassAttachmentInfo
{
    bool                        is_swapchain = false;
    u16                         width = 0, 
                                height = 0;
    VkFormat                    format = VK_FORMAT_UNDEFINED;
    MdRenderPassAttachmentType  type = MD_ATTACHMENT_TYPE_COLOR;
    VkSamplerAddressMode        address[3] = {  VK_SAMPLER_ADDRESS_MODE_REPEAT, 
                                                VK_SAMPLER_ADDRESS_MODE_REPEAT, 
                                                VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkBorderColor               border_color =  VK_BORDER_COLOR_INT_OPAQUE_WHITE;

    MdRenderPassAttachmentInfo(u16 w, u16 h, MdRenderPassAttachmentType type, VkFormat format) : 
        width(w), height(h), type(type), format(format)
    {}

    MdRenderPassAttachmentInfo(){}
};

void mdGetAttachmentTexture(const std::string &name, MdGPUTexture **pp_texture);

void mdRenderGraphInit(MdRenderContext *p_context);
void mdRenderGraphDestroy();
void mdRenderGraphClear();
u32 mdFindRenderPass(const std::string& id);
void mdAddRenderPass(const std::string& id, bool is_swapchain = false);
VkResult mdAddRenderPassInput(  const std::string& id, 
                                const std::string& input, 
                                MdRenderPassAttachmentInfo &info);
VkResult mdAddRenderPassOutput( const std::string& id, 
                                const std::string& output, 
                                MdRenderPassAttachmentInfo &info);
VkResult mdAddRenderPassInput(  const std::string& id, 
                                const std::string& input);
VkResult mdAddRenderPassOutput( const std::string& id, 
                                const std::string& output);
#include <functional>
void mdAddRenderPassFunction(   const std::string& id, 
                                const std::function<void(VkCommandBuffer, VkFramebuffer)> &func);

void mdRenderGraphClearFramebuffers();
VkResult mdRenderGraphGenerateFramebuffers(const std::vector<VkImageView> &swapchain_images);
void mdBuildRenderGraph();
VkRenderPass mdRenderGraphGetPass(const std::string &pass);
VkResult mdPrimeRenderGraph();
usize mdGetCommandBufferCount();
VkResult mdRenderGraphResetBuffers();
void mdExecuteRenderPass(const std::vector<VkClearValue> &values, const std::string &pass, u32 fb_index = 0);
void mdExecuteRenderPass(const std::vector<VkClearValue> &values, u32 pass_index, u32 fb_index = 0);
void mdRenderGraphSubmit(std::vector<VkCommandBuffer> &buffers, bool refill);
#pragma endregion

#pragma region [ Material System ]
struct MdPipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;

    std::array<VkDescriptorSetLayout, 4> set_layouts;
};

struct MdMaterial
{
    VkPipeline pipeline;
    VkDescriptorSet set;
};

VkResult mdCreateDescriptorAllocator(MdRenderer &renderer);
void mdDestroyDescriptorAllocator();

void mdDescriptorSetWriteImage(     MdRenderer &renderer, 
                                    VkDescriptorSet dst, 
                                    u32 binding_index, 
                                    MdGPUTexture &texture, 
                                    VkImageLayout layout);
void mdDescriptorSetWriteUBO(       MdRenderer &renderer, 
                                    VkDescriptorSet dst, 
                                    u32 binding_index, 
                                    usize offset, 
                                    usize range, 
                                    MdGPUBuffer &buffer);

VkResult mdCreateGraphicsPipeline(  MdRenderer &renderer, 
                                    MdShaderSource &shaders, 
                                    MdPipelineGeometryInputState *p_geometry_state, 
                                    MdPipelineRasterizationState *p_raster_state, 
                                    MdPipelineColorBlendState *p_color_blend_state, 
                                    const std::string &pass,
                                    MdPipeline &pipeline);
void mdDestroyPipeline(             MdRenderer &renderer, 
                                    MdPipeline &pipeline);

VkResult mdCreateMaterial(          MdRenderer &renderer, 
                                    MdPipeline &pipeline, 
                                    MdMaterial &material);
void mdMaterialSetImage(            MdRenderer &renderer, 
                                    MdMaterial& material, 
                                    u32 binding_index, 
                                    MdGPUTexture &texture, 
                                    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void mdMaterialSetUBO(              MdRenderer &renderer, 
                                    MdMaterial& material, 
                                    u32 binding_index, 
                                    usize offset, 
                                    usize range, 
                                    MdGPUBuffer &buffer);
#pragma endregion

struct MdFrameData
{
    VkSemaphore image_available, render_finished;
    VkFence in_flight;

    VkCommandPool pool;
    VkCommandBuffer buffer;
};

struct MdGlobalSetUBO
{
    Matrix4x4 shadow_view_projection;
    f32 u_resolution[2];
    f32 u_time;
};

struct MdCameraSetUBO
{
    Matrix4x4 view_projection;
};

VkResult mdCreateGlobalSetsAndLayouts(MdRenderer &renderer);
VkResult mdCreateMainCameraSetsAndLayouts(MdRenderer &renderer);


struct MdRenderState
{
    // Descriptors
    VkDescriptorSetLayout global_layout;
    VkDescriptorSet global_set;

    std::vector<VkDescriptorSet> camera_sets;
    VkDescriptorSetLayout camera_set_layout;

    MdGlobalSetUBO global_set_ubo;
    MdCameraSetUBO camera_set_ubo;

    std::array<VkDescriptorSet, 4> bound_sets;

    MdPipeline shadow_pipeline;
    std::vector<MdPipeline> geometry_pipelines;
    MdPipeline final_pipeline;

    // Allocator
    MdGPUAllocator allocator;
    MdRenderQueue graphics_queue;

    // Frame data (for queue submission and syncing)
    MdFrameData frame_data;
};
void mdGetRenderState(MdRenderState **pp_state);