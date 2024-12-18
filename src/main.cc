#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//#include <SDL2/SDL.h>
//#include <SDL2/SDL_image.h>
//#include <SDL2/SDL_vulkan.h>
#include <vector>
#include <vulkan/vulkan_core.h>

#include <time.h>

#include <VkBootstrap.h>
#include <vma_usage.h>
#include <stb_image_usage.h>

//#define TINYGLTF_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "tinygltf/tiny_gltf.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <typedefs.h>

#define ARCH_AMD64_SSE
#include <simd_math/simd_math.h>
#include <file/file.h>

#define MD_USE_SDL
#define MD_USE_VULKAN
#include <window/window.h>
#include <renderer_vk/renderer_vk_helpers.h>

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

struct MdRenderer
{
    MdRenderContext context;
    MdWindow window;
    MdGPUAllocator allocator;
};

#define EXIT(renderer) {\
    mdDestroyContext(renderer.context);\
    mdDestroyWindow(renderer.window);\
    return -1;\
}

int mdInitVulkan(MdRenderer &renderer)
{
    // Create window
    MdResult result = mdCreateWindow(1920, 1080, "Midori Engine", renderer.window);
    MD_CHECK_ANY(result, -1, "failed to create window");
    
    // Init render context
    std::vector<const char*> instance_extensions;
    u16 count = 0;
    mdWindowQueryRequiredVulkanExtensions(renderer.window, NULL, &count);
    instance_extensions.reserve(count);
    mdWindowQueryRequiredVulkanExtensions(renderer.window, instance_extensions.data(), &count);
    
    result = mdInitContext(renderer.context, instance_extensions);
    if (result != MD_SUCCESS) EXIT(renderer);
    
    mdWindowGetSurfaceKHR(renderer.window, renderer.context.instance, &renderer.context.surface);
    if (renderer.context.surface == VK_NULL_HANDLE) EXIT(renderer);

    result = mdCreateDevice(renderer.context);
    if (result != MD_SUCCESS) EXIT(renderer);

    result = mdGetSwapchain(renderer.context);
    if (result != MD_SUCCESS) EXIT(renderer);

    return 0;
}

struct UBO
{
    Matrix4x4 u_model;
    Matrix4x4 u_view_projection;
    Matrix4x4 u_light_view_projection;
    f32 u_resolution[2];
    f32 u_time;
};

struct MdCamera
{
    Matrix4x4 view, projection;
};

MdResult mdLoadOBJ(const char *p_filepath, float **pp_vertices, usize *p_size)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warning, error;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, p_filepath))
    {
        LOG_ERROR("failed to load obj file: %s %s\n", warning.c_str(), error.c_str());
        return MD_ERROR_OBJ_LOADING_FAILURE;
    }

    // Get vertex count (index count)
    u64 vtx_count = 0;
    for (usize si=0; si<shapes.size(); si++)
        for (usize i=0; i<shapes[si].mesh.num_face_vertices.size(); i++)
            vtx_count += shapes[si].mesh.num_face_vertices[i];
    
    printf("Vertex count: %zu\n", vtx_count);

    float *verts = (float*)malloc(vtx_count*VERTEX_SIZE*sizeof(float));
    if (verts == NULL)
    {
        LOG_ERROR("failed to allocate memory for vertices");
        return MD_ERROR_MEMORY_ALLOCATION_FAILURE;
    }

    // Loop over all materials
    for (usize mi=0; mi<materials.size(); mi++)
        printf("Texture for material[%zu]: %s\n", mi, materials[mi].diffuse_texname.c_str());

    // Loop over all vertices of all faces of all shapes in the mesh
    u64 vertex_index = 0;
    for (usize si=0; si<shapes.size(); si++)
    {
        // Index offset is incremented by the number of verts in a face
        usize index_offset = 0;
        for (usize f=0; f<shapes[si].mesh.num_face_vertices.size(); f++)
        {
            // Get number of verts for the given face
            size_t fv = shapes[si].mesh.num_face_vertices[f];
            bool estimate_normals = false;            

            // Load vertex data
            for (usize v=0; v<fv; v++)
            {
                // Get the list of indices for the given face
                tinyobj::index_t idx = shapes[si].mesh.indices[index_offset + v];
                
                // Vertex positions
                verts[(vertex_index+v)*VERTEX_SIZE + 0] = attrib.vertices[3*((usize)idx.vertex_index)+0];
                verts[(vertex_index+v)*VERTEX_SIZE + 1] = attrib.vertices[3*((usize)idx.vertex_index)+1];
                verts[(vertex_index+v)*VERTEX_SIZE + 2] = attrib.vertices[3*((usize)idx.vertex_index)+2];

                // Normals
                if (idx.normal_index >= 0)
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 3] = attrib.normals[3*((usize)idx.normal_index)+0];
                    verts[(vertex_index+v)*VERTEX_SIZE + 4] = attrib.normals[3*((usize)idx.normal_index)+1];
                    verts[(vertex_index+v)*VERTEX_SIZE + 5] = attrib.normals[3*((usize)idx.normal_index)+2];
                }
                else estimate_normals = true;

                // Texcoords
                if (idx.texcoord_index >= 0)
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 6] = attrib.texcoords[2*(usize)idx.texcoord_index+0];
                    verts[(vertex_index+v)*VERTEX_SIZE + 7] = attrib.texcoords[2*(usize)idx.texcoord_index+1];
                }
                else
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 6] = 0;
                    verts[(vertex_index+v)*VERTEX_SIZE + 7] = 0;
                }
            }

            // Calculate normals using cross product between face edges
            if (estimate_normals)
            {
                // Get edges
                Vector4 edge0(
                    verts[(vertex_index+1)*VERTEX_SIZE + 0]-verts[(vertex_index+0)*VERTEX_SIZE + 0],
                    verts[(vertex_index+1)*VERTEX_SIZE + 1]-verts[(vertex_index+0)*VERTEX_SIZE + 1],
                    verts[(vertex_index+1)*VERTEX_SIZE + 2]-verts[(vertex_index+0)*VERTEX_SIZE + 2],
                    0
                );
                Vector4 edge1(
                    verts[(vertex_index+2)*VERTEX_SIZE + 0]-verts[(vertex_index+0)*VERTEX_SIZE + 0],
                    verts[(vertex_index+2)*VERTEX_SIZE + 1]-verts[(vertex_index+0)*VERTEX_SIZE + 1],
                    verts[(vertex_index+2)*VERTEX_SIZE + 2]-verts[(vertex_index+0)*VERTEX_SIZE + 2],
                    0
                );
                Vector4 n = Vector4::Cross(edge0, edge1);
                
                for (usize v=0; v<fv; v++)
                {
                    verts[(vertex_index+v)*VERTEX_SIZE + 3] = n.xyzw[0];
                    verts[(vertex_index+v)*VERTEX_SIZE + 4] = n.xyzw[1];
                    verts[(vertex_index+v)*VERTEX_SIZE + 5] = n.xyzw[2];
                }
            }

            vertex_index += fv;
            index_offset += fv;
        }
    }

    *pp_vertices = verts;
    *p_size = vtx_count;

    return MD_SUCCESS;
}
/*
struct MdShader
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct MdMaterial
{
    MdShader shader;
    void *data;
    usize size;
};

VkResult mdCreateMaterial()
{
    return VK_ERROR_UNKNOWN;
}
*/
enum MdWindowEventEnum
{
    MD_WINDOW_RESIZED,
    MD_WINDOW_UNCHANGED
};

struct MdWindowEvent
{
    MdWindowEventEnum event;
    u16 nw, nh;
};
MdWindowEvent window_event = {};
int main()
{
    // Init SDL
    if (mdInitWindowSubsystem() != MD_SUCCESS)
    {
        LOG_ERROR("failed to init window subsystem");
        return -1;
    }
    
    MdRenderer renderer;
    if (mdInitVulkan(renderer) != 0)
        return -1;

    // Get graphics queue
    MdRenderQueue graphics_queue;
    MdResult result = mdGetQueue(VK_QUEUE_GRAPHICS_BIT, renderer.context, graphics_queue);
    if (result != MD_SUCCESS) EXIT(renderer);

    // Create command pool and buffer
    MdCommandEncoder cmd_encoder = {};
    VkResult vk_result = mdCreateCommandEncoder(renderer.context, graphics_queue.queue_index, cmd_encoder, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);    
    if (vk_result != VK_SUCCESS)
    {
        LOG_ERROR("failed to create command pool");
        EXIT(renderer);
    }

    vk_result = mdAllocateCommandBuffers(renderer.context, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmd_encoder);
    if (vk_result != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate command buffers");
        EXIT(renderer);
    }

    // Allocator
    MdGPUAllocator gpu_allocator = {};
    mdCreateGPUAllocator(renderer.context, gpu_allocator, graphics_queue, 1024*1024*1024);
    
    // Image texture
    int w = 0, h = 0, bpp = 0;
    stbi_uc *img = stbi_load("../images/test.png", &w, &h, &bpp, 4);
    if (img == NULL)
    {
        LOG_ERROR("failed to load image");
        EXIT(renderer);
    }

    u64 size = w*h*bpp;
    printf("image_size: %zu\n", size);

    MdGPUTexture texture = {};
    MdGPUTextureBuilder tex_builder = {};
    mdCreateTextureBuilder2D(tex_builder, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, bpp);
    mdSetTextureUsage(tex_builder, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mdSetFilterWrap(tex_builder, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    mdSetMipmapOptions(tex_builder, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    mdSetMagFilters(tex_builder, VK_FILTER_LINEAR, VK_FILTER_LINEAR);
    mdBuildTexture2D(renderer.context, tex_builder, gpu_allocator, texture, img);
    
    //SDL_FreeSurface(img_sdl);

    // Depth texture
    MdGPUTexture depth_texture = {};
    MdGPUTextureBuilder depth_tex_builder = {};
    mdCreateTextureBuilder2D(depth_tex_builder, renderer.window.w, renderer.window.h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetTextureUsage(depth_tex_builder, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    vk_result = mdBuildDepthAttachmentTexture2D(renderer.context, depth_tex_builder, gpu_allocator, depth_texture);
    VK_CHECK(vk_result, "failed to create depth texture");

    // Shadow texture
    VkExtent2D shadow_extent;
    shadow_extent.width = 8192;
    shadow_extent.height = 8192;

    MdGPUTexture shadow_texture = {};
    MdGPUTextureBuilder shadow_tex_builder = {};
    mdCreateTextureBuilder2D(shadow_tex_builder, shadow_extent.width, shadow_extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetTextureUsage(shadow_tex_builder, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    mdSetFilterWrap(shadow_tex_builder, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    mdSetTextureBorderColor(shadow_tex_builder, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    vk_result = mdBuildDepthAttachmentTexture2D(renderer.context, shadow_tex_builder, gpu_allocator, shadow_texture);
    VK_CHECK(vk_result, "failed to create shadow texture");

    // Color texture
    MdGPUTexture color_texture = {};
    MdGPUTextureBuilder color_tex_builder = {};
    mdCreateTextureBuilder2D(color_tex_builder, renderer.window.w, renderer.window.h, renderer.context.swapchain.image_format, VK_IMAGE_ASPECT_COLOR_BIT, 4);
    mdSetTextureUsage(color_tex_builder, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mdSetFilterWrap(color_tex_builder, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    mdSetMipmapOptions(color_tex_builder, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    mdSetMagFilters(color_tex_builder, VK_FILTER_LINEAR, VK_FILTER_LINEAR);
    mdBuildColorAttachmentTexture2D(renderer.context, color_tex_builder, gpu_allocator, color_texture);
    
    // Descriptors
    MdDescriptorSetAllocator desc_allocator = {};
    mdCreateDescriptorAllocator(1, 4, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, NULL, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &texture.sampler, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &color_texture.sampler, desc_allocator);
    mdAddDescriptorBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &shadow_texture.sampler, desc_allocator);
    
    vk_result = mdCreateDescriptorSets(1, renderer.context, desc_allocator);
    if (vk_result != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate descriptor sets");
        EXIT(renderer);
    }

    // Vertex buffer
    MdGPUBuffer vertex_buffer = {};
    MdGPUBuffer index_buffer = {};
    float *geometry;
    usize geometry_size;
    mdLoadOBJ("../models/teapot/teapot.obj", &geometry, &geometry_size);

    mdAllocateGPUBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, geometry_size*VERTEX_SIZE*sizeof(f32), gpu_allocator, vertex_buffer);
    mdUploadToGPUBuffer(renderer.context, gpu_allocator, 0, geometry_size*VERTEX_SIZE*sizeof(f32), geometry, vertex_buffer);
    free(geometry);

    Matrix4x4 model;
    Matrix4x4 view;
    Matrix4x4 view_ls;
    // UBO
    MdGPUBuffer uniform_buffer = {};
    UBO ubo = {};
    {
        model = Matrix4x4(
            1, 0, 0, 0,
            0, 1, 0, -2,
            0, 0, 1, -15,
            0, 0, 0, 1
        );
        view = Matrix4x4::LookAt(
            Vector4(0,0,-1,0), 
            Vector4(0,0,1,0), 
            Vector4(0,1,0,0)
        );
        view_ls = Matrix4x4::LookAt(
            Vector4(3,0,-1,0), 
            Vector4(0,0,1,0), 
            Vector4(0,1,0,0)
        );

        ubo.u_resolution[0] = renderer.window.w;
        ubo.u_resolution[1] = renderer.window.h;
        ubo.u_time = 0.0f;
        ubo.u_view_projection = Matrix4x4::Perspective(45., (float)renderer.window.w/(float)renderer.window.h, 0.1f, 1000.0f) * view;
        ubo.u_light_view_projection = Matrix4x4::Orthographic(-10, 10, -10, 10, 0.1, 1000) * view_ls;
        ubo.u_model = model;

        mdAllocateGPUUniformBuffer(sizeof(ubo), gpu_allocator, uniform_buffer);
        mdUploadToUniformBuffer(renderer.context, gpu_allocator, 0, sizeof(ubo), &ubo, uniform_buffer);
    }

    // Shadow pass
    MdRenderTarget render_target_shadow = {};
    MdPipelineState pipeline_shadow;
    {
        MdRenderTargetBuilder builder_shadow = {};
    
        mdCreateRenderTargetBuilder(shadow_extent.width, shadow_extent.height, builder_shadow);
        mdRenderTargetAddDepthAttachment(builder_shadow, depth_texture.format);

        vk_result = mdBuildRenderTarget(renderer.context, builder_shadow, render_target_shadow);
        VK_CHECK(vk_result, "failed to build render target");

        std::vector<VkImageView> attachments;
        attachments.push_back(shadow_texture.image_view);
        
        vk_result = mdRenderTargetAddFramebuffer(renderer.context, render_target_shadow, attachments);
        VK_CHECK(vk_result, "failed to add framebuffer");

        MdPipelineGeometryInputState geometry_state = {}; 
        MdPipelineRasterizationState raster_state = {};
        MdPipelineColorBlendState color_blend_state = {};
        
        // Shaders
        MdShaderSource source;
        vk_result = mdLoadShaderSPIRVFromFile(renderer.context, "../shaders/spv/test_shadow_vert.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load vertex shader");
            EXIT(renderer);
        }

        vk_result = mdLoadShaderSPIRVFromFile(renderer.context, "../shaders/spv/test_shadow_frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load fragment shader");
            EXIT(renderer);
        }

        mdInitGeometryInputState(geometry_state);
        mdGeometryInputAddVertexBinding(geometry_state, VK_VERTEX_INPUT_RATE_VERTEX, 8*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state, 0, 0, 3, MD_F32, 0);
        mdGeometryInputAddAttribute(geometry_state, 0, 1, 3, MD_F32, 3*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state, 0, 2, 2, MD_F32, 6*sizeof(f32));
        mdBuildGeometryInputState(geometry_state);

        mdBuildDefaultRasterizationState(raster_state);

        mdBuildDefaultColorBlendState(color_blend_state);

        vk_result = mdCreateGraphicsPipeline(
            renderer.context, 
            source, 
            1,
            &desc_allocator,
            &geometry_state,
            &raster_state,
            &color_blend_state,
            render_target_shadow,
            1, 
            pipeline_shadow
        );
        mdDestroyShaderSource(renderer.context, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics pipeline");
            EXIT(renderer);
        }
    }

    // Render pass A
    MdRenderTarget render_target_A = {};
    MdPipelineState pipeline_A;
    {   
        MdRenderTargetBuilder builder_A = {};
    
        mdCreateRenderTargetBuilder(renderer.window.w, renderer.window.h, builder_A);
        mdRenderTargetAddColorAttachment(builder_A, renderer.context.swapchain.image_format);
        mdRenderTargetAddDepthAttachment(builder_A, depth_texture.format);

        vk_result = mdBuildRenderTarget(renderer.context, builder_A, render_target_A);
        VK_CHECK(vk_result, "failed to build render target");

        std::vector<VkImageView> attachments;
        attachments.push_back(color_texture.image_view);
        attachments.push_back(depth_texture.image_view);
        
        vk_result = mdRenderTargetAddFramebuffer(renderer.context, render_target_A, attachments);
        VK_CHECK(vk_result, "failed to add framebuffer");

        MdPipelineGeometryInputState geometry_state_A = {}; 
        MdPipelineRasterizationState raster_state_A = {};
        MdPipelineColorBlendState color_blend_state_A = {};
        
        // Shaders
        MdShaderSource source;
        vk_result = mdLoadShaderSPIRVFromFile(renderer.context, "../shaders/spv/test_vert.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load vertex shader");
            EXIT(renderer);
        }

        vk_result = mdLoadShaderSPIRVFromFile(renderer.context, "../shaders/spv/test_frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load fragment shader");
            EXIT(renderer);
        }

        mdInitGeometryInputState(geometry_state_A);
        mdGeometryInputAddVertexBinding(geometry_state_A, VK_VERTEX_INPUT_RATE_VERTEX, 8*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state_A, 0, 0, 3, MD_F32, 0);
        mdGeometryInputAddAttribute(geometry_state_A, 0, 1, 3, MD_F32, 3*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state_A, 0, 2, 2, MD_F32, 6*sizeof(f32));
        mdBuildGeometryInputState(geometry_state_A);

        mdBuildDefaultRasterizationState(raster_state_A);

        mdBuildDefaultColorBlendState(color_blend_state_A);

        vk_result = mdCreateGraphicsPipeline(
            renderer.context, 
            source, 
            1,
            &desc_allocator,
            &geometry_state_A,
            &raster_state_A,
            &color_blend_state_A,
            render_target_A,
            1, 
            pipeline_A
        );
        mdDestroyShaderSource(renderer.context, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics pipeline");
            EXIT(renderer);
        }
    }

    // Render pass B
    MdRenderTarget render_target_B = {};
    MdPipelineState pipeline_B;
    {   
        MdRenderTargetBuilder builder_B = {};
    
        mdCreateRenderTargetBuilder(renderer.window.w, renderer.window.h, builder_B);
        mdRenderTargetAddColorAttachment(builder_B, renderer.context.swapchain.image_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        
        vk_result = mdBuildRenderTarget(renderer.context, builder_B, render_target_B);
        VK_CHECK(vk_result, "failed to build render target");

        std::vector<VkImageView> attachments;
        attachments.push_back(VK_NULL_HANDLE);

        for (u32 i=0; i<renderer.context.sw_image_views.size(); i++)
        {
            attachments[0] = renderer.context.sw_image_views[i];
            vk_result = mdRenderTargetAddFramebuffer(renderer.context, render_target_B, attachments);
            VK_CHECK(vk_result, "failed to add framebuffer");
        }

        MdPipelineGeometryInputState geometry_state_B = {}; 
        MdPipelineRasterizationState raster_state_B = {};
        MdPipelineColorBlendState color_blend_state_B = {};
        
        // Shaders
        MdShaderSource source;
        vk_result = mdLoadShaderSPIRVFromFile(renderer.context, "../shaders/spv/test_vert_2.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load vertex shader");
            EXIT(renderer);
        }

        vk_result = mdLoadShaderSPIRVFromFile(renderer.context, "../shaders/spv/test_frag_2.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load fragment shader");
            EXIT(renderer);
        }

        mdInitGeometryInputState(geometry_state_B);
        mdBuildGeometryInputState(geometry_state_B);

        mdBuildDefaultRasterizationState(raster_state_B);

        mdBuildDefaultColorBlendState(color_blend_state_B);

        vk_result = mdCreateGraphicsPipeline(
            renderer.context, 
            source, 
            1,
            &desc_allocator,
            &geometry_state_B,
            &raster_state_B,
            &color_blend_state_B,
            render_target_B,
            1, 
            pipeline_B
        );
        mdDestroyShaderSource(renderer.context, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics pipeline");
            EXIT(renderer);
        }
    }

    // Fences
    VkSemaphore image_available, render_finished;
    VkFence in_flight;
    {
        VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    
        semaphore_info.flags = 0;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
        vkCreateSemaphore(renderer.context.device, &semaphore_info, NULL, &image_available);
        vkCreateSemaphore(renderer.context.device, &semaphore_info, NULL, &render_finished);
        vkCreateFence(renderer.context.device, &fence_info, NULL, &in_flight);
    }

    // Viewport
    VkViewport viewport = {};
    VkRect2D scissor = {};
    VkViewport shadow_viewport = {};
    VkRect2D shadow_scissor = {};
    {
        // Regular viewport
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = renderer.window.w;
        viewport.height = renderer.window.h;

        scissor.offset = {0,0};
        scissor.extent = {renderer.window.w, renderer.window.h};

        // Shadow viewport
        shadow_viewport.minDepth = 0.0f;
        shadow_viewport.maxDepth = 1.0f;
        shadow_viewport.x = 0;
        shadow_viewport.y = 0;
        shadow_viewport.width = shadow_extent.width;
        shadow_viewport.height = shadow_extent.height;

        shadow_scissor.offset = {0,0};
        shadow_scissor.extent = shadow_extent;
    }
    // Main render loop
    bool quit = false;
    
    u32 image_index = 0;
    i32 max_frames = -1;
    u32 frame_count = 0;

    mdUpdateDescriptorSetImage(renderer.context, desc_allocator, 1, 0, texture);
    mdUpdateDescriptorSetImage(renderer.context, desc_allocator, 2, 0, color_texture);
    mdUpdateDescriptorSetImage(renderer.context, desc_allocator, 3, 0, shadow_texture);
    
    VkDeviceSize offsets[1] = {0};
    window_event.event = MD_WINDOW_UNCHANGED;

    mdWindowRegisterWindowResizedCallback(renderer.window, [](u16 w, u16 h){
        window_event.event = MD_WINDOW_RESIZED;
        window_event.nw = w;
        window_event.nh = h;
    });
    do 
    {
        if (window_event.event == MD_WINDOW_RESIZED)
        {
            vkDeviceWaitIdle(renderer.context.device);
            // TO-DO: Rebuild Swapchain
        }
        vkWaitForFences(renderer.context.device, 1, &in_flight, VK_TRUE, UINT64_MAX);
        
        if (max_frames > -1 && frame_count++ >= max_frames)
            break;

        vk_result = vkAcquireNextImageKHR(
            renderer.context.device, 
            renderer.context.swapchain, 
            UINT64_MAX, 
            image_available, 
            VK_NULL_HANDLE, 
            &image_index
        );
        
        if (vk_result != VK_SUCCESS)
        {
            // Rebuild swapchain
            if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                window_event.event = MD_WINDOW_RESIZED;
                continue;
            }
            else break;
        }

        vkResetFences(renderer.context.device, 1, &in_flight);
        
        // Update descriptors
        {
            ubo.u_time = mdGetTicks() / 1000.0f;
            mdUploadToUniformBuffer(renderer.context, gpu_allocator, 0, sizeof(ubo), &ubo, uniform_buffer);
            mdUpdateDescriptorSetUBO(
                renderer.context, 
                desc_allocator, 
                0, 
                0, 
                uniform_buffer, 
                0, 
                sizeof(ubo)
            );
        }
        
        // Command recording
        {
            VkClearValue clear_values[2];
            clear_values[0] = {.8, .8, .8, 1};
            clear_values[1].depthStencil = {1.0f, 0};

            VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin_info.pInheritanceInfo = NULL;
            begin_info.flags = 0;
            vkResetCommandBuffer(cmd_encoder.buffers[0], 0);
            vkBeginCommandBuffer(cmd_encoder.buffers[0], &begin_info);

            // Shadow pass
            {
                VkRenderPassBeginInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rp_info.renderPass = render_target_shadow.pass;
                rp_info.renderArea.offset = {0,0};
                rp_info.renderArea.extent = shadow_extent;
                rp_info.clearValueCount = 1;
                rp_info.pClearValues = &clear_values[1];
                rp_info.framebuffer = render_target_shadow.buffers[0];
                vkCmdBeginRenderPass(cmd_encoder.buffers[0], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdSetViewport(cmd_encoder.buffers[0], 0, 1, &shadow_viewport);
                vkCmdSetScissor(cmd_encoder.buffers[0], 0, 1, &shadow_scissor);

                vkCmdBindVertexBuffers(
                    cmd_encoder.buffers[0], 
                    0, 
                    1, 
                    &vertex_buffer.buffer, offsets
                );
                vkCmdBindDescriptorSets(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_shadow.layout, 
                    0, 
                    desc_allocator.sets.size(), 
                    desc_allocator.sets.data(), 
                    0, 
                    NULL
                );
                vkCmdBindPipeline(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_shadow.pipeline[0]
                );
                vkCmdDraw(cmd_encoder.buffers[0], geometry_size, 1, 0, 0);
                vkCmdEndRenderPass(cmd_encoder.buffers[0]);
            }

            // Pass A
            {
                mdTransitionImageLayout(
                    shadow_texture, 
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
                    VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    cmd_encoder.buffers[0]
                );

                VkRenderPassBeginInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rp_info.renderPass = render_target_A.pass;
                rp_info.renderArea.offset = {0,0};
                rp_info.renderArea.extent = {renderer.window.w, renderer.window.h};
                rp_info.clearValueCount = 2;
                rp_info.pClearValues = clear_values;
                rp_info.framebuffer = render_target_A.buffers[0];
                vkCmdBeginRenderPass(cmd_encoder.buffers[0], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdSetViewport(cmd_encoder.buffers[0], 0, 1, &viewport);
                vkCmdSetScissor(cmd_encoder.buffers[0], 0, 1, &scissor);

                vkCmdBindVertexBuffers(
                    cmd_encoder.buffers[0], 
                    0, 
                    1, 
                    &vertex_buffer.buffer, offsets
                );
                vkCmdBindDescriptorSets(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_A.layout, 
                    0, 
                    desc_allocator.sets.size(), 
                    desc_allocator.sets.data(), 
                    0, 
                    NULL
                );
                vkCmdBindPipeline(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_A.pipeline[0]
                );
                vkCmdDraw(cmd_encoder.buffers[0], geometry_size, 1, 0, 0);
                vkCmdEndRenderPass(cmd_encoder.buffers[0]);
            }
            
            // Pass B
            {
                mdTransitionImageLayout(
                    color_texture, 
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                    VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    cmd_encoder.buffers[0]
                );
                VkRenderPassBeginInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rp_info.renderPass = render_target_B.pass;
                rp_info.renderArea.offset = {0,0};
                rp_info.renderArea.extent = {renderer.window.w, renderer.window.h};
                rp_info.clearValueCount = 2;
                rp_info.pClearValues = clear_values;
                rp_info.framebuffer = render_target_B.buffers[image_index];
                vkCmdBeginRenderPass(cmd_encoder.buffers[0], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdSetViewport(cmd_encoder.buffers[0], 0, 1, &viewport);
                vkCmdSetScissor(cmd_encoder.buffers[0], 0, 1, &scissor);
                vkCmdBindDescriptorSets(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_B.layout, 
                    0, 
                    desc_allocator.sets.size(), 
                    desc_allocator.sets.data(), 
                    0, 
                    NULL
                );
                vkCmdBindPipeline(
                    cmd_encoder.buffers[0], 
                    VK_PIPELINE_BIND_POINT_GRAPHICS, 
                    pipeline_B.pipeline[0]
                );
                vkCmdDraw(cmd_encoder.buffers[0], 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd_encoder.buffers[0]);
            }
            
            vkEndCommandBuffer(cmd_encoder.buffers[0]);
        }

        // Submit to queue and present image
        {
            VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd_encoder.buffers[0];
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &image_available;
            submit_info.pWaitDstStageMask = &wait_stages;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &render_finished;
            vk_result = vkQueueSubmit(graphics_queue.queue_handle, 1, &submit_info, in_flight);
            if (vk_result != VK_SUCCESS)
                break;
            
            VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &renderer.context.swapchain.swapchain;
            present_info.pImageIndices = &image_index;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &render_finished;
            vk_result = vkQueuePresentKHR(graphics_queue.queue_handle, &present_info); 
            if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                window_event.event = MD_WINDOW_RESIZED;
            }
        }
        
        mdPollEvent(renderer.window);
    }
    while(!mdWindowShouldClose(renderer.window));

    vkQueueWaitIdle(graphics_queue.queue_handle);

    // Destroy fences and semaphores
    vkDestroySemaphore(renderer.context.device, image_available, NULL);
    vkDestroySemaphore(renderer.context.device, render_finished, NULL);
    vkDestroyFence(renderer.context.device, in_flight, NULL);

    // Destroy texture
    mdDestroyTexture(gpu_allocator, texture);

    // Destroy render pass
    mdDestroyRenderTarget(renderer.context, render_target_A);
    mdDestroyRenderTarget(renderer.context, render_target_B);
    mdDestroyRenderTarget(renderer.context, render_target_shadow);
    mdDestroyAttachmentTexture(gpu_allocator, color_texture);
    mdDestroyAttachmentTexture(gpu_allocator, depth_texture);
    mdDestroyAttachmentTexture(gpu_allocator, shadow_texture);

    // Free GPU memory
    mdFreeGPUBuffer(gpu_allocator, uniform_buffer);
    mdFreeGPUBuffer(gpu_allocator, vertex_buffer);
    mdDestroyGPUAllocator(gpu_allocator);

    mdDestroyPipelineState(renderer.context, pipeline_A);
    mdDestroyPipelineState(renderer.context, pipeline_B);
    mdDestroyPipelineState(renderer.context, pipeline_shadow);
    
    mdDestroyDescriptorSetAllocator(renderer.context, desc_allocator);
    mdDestroyCommandEncoder(renderer.context, cmd_encoder);

    // Destroy context and window
    mdDestroyContext(renderer.context);
    mdDestroyWindow(renderer.window);

    mdDestroyWindowSubsystem();
    return 0;
}