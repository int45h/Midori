#include <vector>
#include <vulkan/vulkan_core.h>
#define MD_USE_VULKAN
#include <renderer.h>
#include <platform/file/file.h>
#include <platform/window/window.h>
#include <simd_math.h>

struct MdCamera
{
    Matrix4x4 view_projection;
};

typedef u32 MdPipelineHandle;
typedef u32 MdMaterialHandle;

/*
    Material:
    - Pipeline
    - Set Index
    - Hashmap:
        - Binding Index
        - Data
*/

/*
    Mesh:
    - Vertex/Index buffer
*/

/*
    Model:
    - Mesh
    - Material
*/

struct MdMaterialData
{
    void *data;
    usize size;
};

struct MdMaterial
{
    MdPipelineHandle handle;
    std::vector<MdMaterialData> data;
};

MdRenderContext context;

/* Private vars and functions */
struct MdMaterialList
{
    std::vector<MdPipelineState> pipelines;
    std::vector<MdMaterial> materials;
};

MdMaterialList material_list;
VkExtent2D shadow_extent = {8192, 8192};

struct MdRenderState
{
    // Descriptors
    MdDescriptorSetAllocator global_sets;
    std::vector<MdDescriptorSetAllocator> camera_sets;
    std::vector<MdDescriptorSetAllocator> material_sets;
    MdDescriptorSetAllocator final_sets;

    // Built-in textures
    MdGPUTextureBuilder shadow_tex_builder; // TO-DO: Allow user to make directional lights which have shadow maps
    MdGPUTexture shadow_texture;
    MdGPUTextureBuilder depth_tex_builder; // Geometry pass depth and color texture
    MdGPUTexture depth_attachment;
    MdGPUTextureBuilder color_tex_builder;
    MdGPUTexture color_attachment;
    
    // Passes
    MdPipelineState pipeline_shadow;
    MdRenderTarget pass_shadow;
    MdRenderTarget pass_geometry;
    std::vector<MdPipelineState> pipeline_post_fx;
    std::vector<MdRenderTarget> pass_post_fx; 

    MdPipelineState pipeline_final; // Final composite pass
    MdRenderTarget pass_final;

    // Allocator
    MdGPUAllocator allocator;
    MdRenderQueue graphics_queue;
};
MdRenderState renderer_state;

VkResult mdLoadShaderSPIRVFromFile( MdRenderContext &context, 
                                    const char *p_filepath,
                                    VkShaderStageFlagBits stage,
                                    MdShaderSource &source)
{
    MdFile file = {};
    MdResult md_result = mdOpenFile(p_filepath, MD_FILE_ACCESS_READ_ONLY, file);
    MD_CHECK_ANY(md_result, VK_ERROR_UNKNOWN, "failed to load file");

    u32 *code = (u32*)malloc(file.size);
    u32 size = (file.size / 4) * 4;

    md_result = mdReadFile(file, size, code);
    MD_CHECK_ANY(md_result, VK_ERROR_UNKNOWN, "failed to copy file to memory");

    mdCloseFile(file);

    VkResult result = mdLoadShaderSPIRV(context, size, code, stage, source);
    free(code);

    return result;
}

VkResult mdCreateEmptyPipeline(MdRenderer &renderer, MdRenderTarget &target, MdPipelineState &pipeline)
{
    // Empty pipeline (a fallback)
    MdPipelineGeometryInputState geometry_state_A = {}; 
    MdPipelineRasterizationState raster_state_A = {};
    MdPipelineColorBlendState color_blend_state_A = {};

    MdShaderSource source;
    VkResult result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/empty.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
    VK_CHECK(result, "failed to load empty vertex shader");

    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/empty.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
    VK_CHECK(result, "failed to load empty vertex shader");

    mdInitGeometryInputState(geometry_state_A);
    mdBuildGeometryInputState(geometry_state_A);
    mdBuildDefaultRasterizationState(raster_state_A);
    mdBuildDefaultColorBlendState(color_blend_state_A);

    result = mdCreateGraphicsPipeline(
        *renderer.context, 
        source, 
        0,
        NULL,
        &geometry_state_A,
        &raster_state_A,
        &color_blend_state_A,
        target,
        1, 
        pipeline
    );
    mdDestroyShaderSource(*renderer.context, source);
    VK_CHECK(result, "failed to create empty pipeline");
    
    return result;
}

VkResult mdCreateShadowPass(MdRenderer &renderer)
{
    VkResult result;

    mdCreateTextureBuilder2D(renderer_state.shadow_tex_builder, shadow_extent.width, shadow_extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetTextureUsage(renderer_state.shadow_tex_builder, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetFilterWrap(renderer_state.shadow_tex_builder, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    mdSetTextureBorderColor(renderer_state.shadow_tex_builder, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    result = mdBuildDepthAttachmentTexture2D(*renderer.context, renderer_state.shadow_tex_builder, renderer_state.allocator, renderer_state.shadow_texture);
    VK_CHECK(result, "failed to create shadow texture");

    MdRenderTargetBuilder builder_shadow = {};
    mdCreateRenderTargetBuilder(shadow_extent.width, shadow_extent.height, builder_shadow);
    mdRenderTargetAddDepthAttachment(builder_shadow, renderer_state.shadow_texture.format);
    result = mdBuildRenderTarget(*renderer.context, builder_shadow, renderer_state.pass_shadow);
    VK_CHECK(result, "failed to build render target");

    std::vector<VkImageView> attachments;
    attachments.push_back(renderer_state.shadow_texture.image_view);
        
    result = mdRenderTargetAddFramebuffer(*renderer.context, renderer_state.pass_shadow, attachments);
    VK_CHECK(result, "failed to add framebuffer");

    MdPipelineGeometryInputState geometry_state = {}; 
    MdPipelineRasterizationState raster_state = {};
    MdPipelineColorBlendState color_blend_state = {};
        
    // Shaders
    MdShaderSource source;
    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_shadow_vert.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
    VK_CHECK(result, "failed to load shadow pass vertex shader");
    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_shadow_frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
    VK_CHECK(result, "failed to load shadow pass fragment shader");

    mdInitGeometryInputState(geometry_state);
    mdGeometryInputAddVertexBinding(geometry_state, VK_VERTEX_INPUT_RATE_VERTEX, 8*sizeof(f32));
    mdGeometryInputAddAttribute(geometry_state, 0, 0, 3, MD_F32, 0);
    mdGeometryInputAddAttribute(geometry_state, 0, 1, 3, MD_F32, 3*sizeof(f32));
    mdGeometryInputAddAttribute(geometry_state, 0, 2, 2, MD_F32, 6*sizeof(f32));
    mdBuildGeometryInputState(geometry_state);

    mdBuildDefaultRasterizationState(raster_state);
    mdBuildDefaultColorBlendState(color_blend_state);

    result = mdCreateGraphicsPipeline(
        *renderer.context, 
        source, 
        1,
        &renderer_state.global_sets,
        &geometry_state,
        &raster_state,
        &color_blend_state,
        renderer_state.pass_shadow,
        1, 
        renderer_state.pipeline_shadow
    );
    mdDestroyShaderSource(*renderer.context, source);

    return result;
}

VkResult mdCreateGeometryPass(MdRenderer &renderer)
{
    // Render pass A
    VkResult result;

    // Depth texture
    mdCreateTextureBuilder2D(renderer_state.depth_tex_builder, renderer.window.w, renderer.window.h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetTextureUsage(renderer_state.depth_tex_builder, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    result = mdBuildDepthAttachmentTexture2D(*renderer.context, renderer_state.depth_tex_builder, renderer_state.allocator, renderer_state.depth_attachment);
    VK_CHECK(result, "failed to create depth attachment");
    
    // Color texture
    mdCreateTextureBuilder2D(renderer_state.color_tex_builder, renderer.window.w, renderer.window.h, renderer.context->swapchain.image_format, VK_IMAGE_ASPECT_COLOR_BIT, 4);
    mdSetTextureUsage(renderer_state.color_tex_builder, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mdSetFilterWrap(renderer_state.color_tex_builder, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    mdSetMipmapOptions(renderer_state.color_tex_builder, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    mdSetMagFilters(renderer_state.color_tex_builder, VK_FILTER_LINEAR, VK_FILTER_LINEAR);
    result = mdBuildColorAttachmentTexture2D(*renderer.context, renderer_state.color_tex_builder, renderer_state.allocator, renderer_state.color_attachment);
    VK_CHECK(result, "failed to create color attachment");
    
    MdRenderTargetBuilder builder_A = {};
    
    mdCreateRenderTargetBuilder(renderer.window.w, renderer.window.h, builder_A);
    mdRenderTargetAddColorAttachment(builder_A, renderer.context->swapchain.image_format);
    mdRenderTargetAddDepthAttachment(builder_A, renderer_state.depth_attachment.format);

    result = mdBuildRenderTarget(*renderer.context, builder_A, renderer_state.pass_geometry);
    VK_CHECK(result, "failed to build render target");

    std::vector<VkImageView> attachments;
    attachments.push_back(renderer_state.color_attachment.image_view);
    attachments.push_back(renderer_state.depth_attachment.image_view);
        
    result = mdRenderTargetAddFramebuffer(*renderer.context, renderer_state.pass_geometry, attachments);
    VK_CHECK(result, "failed to add framebuffer");

    return result;
}

VkResult mdCreateFinalPass(MdRenderer &renderer)
{
    // Render pass B
    MdRenderTargetBuilder builder_B = {};
    
    mdCreateRenderTargetBuilder(renderer.window.w, renderer.window.h, builder_B);
    mdRenderTargetAddColorAttachment(builder_B, renderer.context->swapchain.image_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        
    VkResult result = mdBuildRenderTarget(*renderer.context, builder_B, renderer_state.pass_final);
    VK_CHECK(result, "failed to build render target");

    std::vector<VkImageView> attachments;
    attachments.push_back(VK_NULL_HANDLE);

    for (u32 i=0; i<renderer.context->sw_image_views.size(); i++)
    {
        attachments[0] = renderer.context->sw_image_views[i];
        result = mdRenderTargetAddFramebuffer(*renderer.context, renderer_state.pass_final, attachments);
        VK_CHECK(result, "failed to add framebuffer");
    }

    MdPipelineGeometryInputState geometry_state_B = {}; 
    MdPipelineRasterizationState raster_state_B = {};
    MdPipelineColorBlendState color_blend_state_B = {};
        
    // Shaders
    MdShaderSource source;
    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_vert_2.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
    VK_CHECK(result, "failed to load final vertex shader");

    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_frag_2.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
    VK_CHECK(result, "failed to load final fragment shader");

    mdInitGeometryInputState(geometry_state_B);
    mdBuildGeometryInputState(geometry_state_B);

    mdBuildDefaultRasterizationState(raster_state_B);

    mdBuildDefaultColorBlendState(color_blend_state_B);

    result = mdCreateGraphicsPipeline(
        *renderer.context, 
        source, 
        1,
        &renderer_state.global_sets,
        &geometry_state_B,
        &raster_state_B,
        &color_blend_state_B,
        renderer_state.pass_final,
        1, 
        renderer_state.pipeline_final
    );
    mdDestroyShaderSource(*renderer.context, source);
    VK_CHECK(result, "failed to create final pass pipeline");
    
    return result;
}

MdResult mdCreateRendererState(MdRenderer &renderer)
{
    // Create graphics queue
    MdResult result = mdGetQueue(VK_QUEUE_GRAPHICS_BIT, *renderer.context, renderer_state.graphics_queue);
    if (result != MD_SUCCESS)
    {
        mdDestroyRenderer(renderer);
        return result;
    }

    if (mdCreateGPUAllocator(
        *renderer.context, 
        renderer_state.allocator, 
        renderer_state.graphics_queue
    ) != VK_SUCCESS)
    {
        mdDestroyRenderer(renderer);
        return MD_ERROR_UNKNOWN;
    }

    return result;
}

void mdDestroyRendererState(MdRenderer &renderer)
{
    mdDestroyGPUAllocator(renderer_state.allocator);
}

/* Public functions */
MdResult mdCreateRenderer(u16 w, u16 h, const char *p_title, MdRenderer &renderer)
{
    MdResult result = mdInitWindowSubsystem();
    if (result != MD_SUCCESS) return result;

    result = mdCreateWindow(w, h, p_title, renderer.window);
    if (result != MD_SUCCESS) return result;

    // Init render context
    std::vector<const char*> instance_extensions;
    u16 count = 0;
    mdWindowQueryRequiredVulkanExtensions(renderer.window, NULL, &count);
    instance_extensions.reserve(count);
    mdWindowQueryRequiredVulkanExtensions(renderer.window, instance_extensions.data(), &count);
    
    renderer.context = &context;
    result = mdInitContext(*renderer.context, instance_extensions);
    if (result != MD_SUCCESS) 
    {
        mdDestroyRenderer(renderer);
        return result;
    }
    
    mdWindowGetSurfaceKHR(renderer.window, renderer.context->instance, &renderer.context->surface);
    if (renderer.context->surface == VK_NULL_HANDLE)
    {
        mdDestroyRenderer(renderer);
        return result;
    }
    
    result = mdCreateDevice(*renderer.context);
    if (result != MD_SUCCESS)
    {
        mdDestroyRenderer(renderer);
        return result;
    }

    result = mdGetSwapchain(*renderer.context);
    if (result != MD_SUCCESS)
    {
        mdDestroyRenderer(renderer);
        return result;
    }

    return MD_SUCCESS;
}

void mdDestroyRenderer(MdRenderer &renderer)
{
    mdDestroyRendererState(renderer);
    
    mdDestroyContext(*renderer.context);
    mdDestroyWindow(renderer.window);
    mdDestroyWindowSubsystem();
}