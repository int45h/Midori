#include <cstddef>
#include <cstdlib>
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
#include <renderer.h>

struct MdCamera
{
    Matrix4x4 view_projection;
};

#define MD_NULL_HANDLE 0xFFFFFFFF 
VkExtent2D shadow_extent = {8192, 8192};
MdRenderState *p_renderer_state;

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

VkResult mdCreateShadowPass(MdRenderer &renderer)
{
    VkResult result;
    
    MdRenderPassAttachmentInfo shadow_info = {};
    shadow_info.type = MD_ATTACHMENT_TYPE_DEPTH;
    shadow_info.format = VK_FORMAT_D32_SFLOAT;
    shadow_info.border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    shadow_info.address[0] = 
    shadow_info.address[1] = 
    shadow_info.address[2] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    //shadow_info.width = shadow_extent.width;
    //shadow_info.height = shadow_extent.height;

    mdAddRenderPass("shadow");
    result = mdAddRenderPassOutput("shadow", "shadow_map", shadow_info);
    if (result != VK_SUCCESS)
        LOG_ERROR("failed to create shadow pass");

    return result;
}

VkResult mdCreateGeometryPass(MdRenderer &renderer)
{
    // Render pass A
    VkResult result;
    
    MdRenderPassAttachmentInfo geometry_info = {};
    geometry_info.is_swapchain = true;
    geometry_info.type = MD_ATTACHMENT_TYPE_COLOR;
    geometry_info.format = VK_FORMAT_B8G8R8A8_SRGB;
    geometry_info.border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    geometry_info.address[0] = 
    geometry_info.address[1] = 
    geometry_info.address[2] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    // depth buffer
    MdRenderPassAttachmentInfo depth_info = {};
    depth_info.is_swapchain = false;
    depth_info.type = MD_ATTACHMENT_TYPE_DEPTH;
    depth_info.format = VK_FORMAT_D32_SFLOAT;
    depth_info.border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    depth_info.address[0] = 
    depth_info.address[1] = 
    depth_info.address[2] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    mdAddRenderPass("geometry");
    result = mdAddRenderPassOutput("geometry", "color_tex1", geometry_info);
    VK_CHECK(result, "failed to create geometry pass");
    result = mdAddRenderPassOutput("geometry", "depth_tex1", depth_info);
    VK_CHECK(result, "failed to create geometry pass");
    
    mdAddRenderPassInput("geometry", "shadow_map");
    
    MdGPUTexture *color_texture;
    mdGetAttachmentTexture("color_tex1", &color_texture);
    if (color_texture == NULL)
    {
        LOG_ERROR("failed to retrieve color texture");
        return VK_ERROR_UNKNOWN;
    }

    return result;
}

VkResult mdCreateFinalPass(MdRenderer &renderer)
{
    // Render pass B
    VkResult result;

    MdRenderPassAttachmentInfo final_info = {};
    final_info.type = MD_ATTACHMENT_TYPE_COLOR;
    final_info.border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    final_info.address[0] = 
    final_info.address[1] = 
    final_info.address[2] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    mdAddRenderPass("final", true);
    result = mdAddRenderPassInput("final", "color_tex1", final_info);
    VK_CHECK(result, "failed to create final pass");

    return result;
}

VkResult mdCreateShadowPassPipeline(MdRenderer &renderer)
{
    VkResult result;
    MdGPUTexture *shadow_texture;
    mdGetAttachmentTexture("shadow_map", &shadow_texture);
    if (shadow_texture == NULL)
    {
        LOG_ERROR("failed to retrieve shadow texture");
        return VK_ERROR_UNKNOWN;
    }

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

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back({
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = NULL
    });

    result = mdCreateGraphicsPipeline(
        renderer, 
        source, 
        &geometry_state, 
        &raster_state, 
        &color_blend_state, 
        "shadow", 
        p_renderer_state->shadow_pipeline
    );
    mdDestroyShaderSource(*renderer.context, source);
    VK_CHECK(result, "failed to create shadow pipeline\n");
    
    return result;
}

VkResult mdCreateFinalPassPipeline(MdRenderer &renderer, MdGPUTexture *color_attachment, MdGPUTexture *shadow_texture, MdMaterial &material)
{
    VkResult result;
    MdPipelineGeometryInputState geometry_state = {}; 
    MdPipelineRasterizationState raster_state = {};
    MdPipelineColorBlendState color_blend_state = {};
        
    // Shaders
    MdShaderSource source;
    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_vert_2.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
    VK_CHECK(result, "failed to load final vertex shader");

    result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_frag_2.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
    VK_CHECK(result, "failed to load final fragment shader");

    mdShaderAddBinding(source, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &color_attachment->sampler);
    mdShaderAddBinding(source, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &shadow_texture->sampler);

    mdInitGeometryInputState(geometry_state);
    mdBuildGeometryInputState(geometry_state);
    mdBuildDefaultRasterizationState(raster_state);
    mdBuildDefaultColorBlendState(color_blend_state);
    
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back({
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = NULL
    });

    result = mdCreateGraphicsPipeline(
        renderer, 
        source, 
        &geometry_state, 
        &raster_state, 
        &color_blend_state,
        "final",
        p_renderer_state->final_pipeline
    );
    VK_CHECK(result, "failed to create final pass pipeline");
    
    result = mdCreateMaterial(renderer, p_renderer_state->final_pipeline, material);
    VK_CHECK(result, "failed to create material for final pass");

    mdDestroyShaderSource(*renderer.context, source);
    return result;
}

#define EXIT(renderer) {\
    mdDestroyRenderer(renderer);\
    return -1;\
}

struct UBO
{
    Matrix4x4 u_model;
    Matrix4x4 u_view_projection;
    Matrix4x4 u_light_view_projection;
    f32 u_resolution[2];
    f32 u_time;
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

struct MdModel
{
    MdGPUBuffer         vertex_buffer, 
                        index_buffer;
    MdGPUTexture        texture;
    MdGPUTextureBuilder texture_builder;
    usize               geometry_size;
};

MdResult mdLoadTextureFromPath(MdRenderer &renderer, const std::string& path, MdGPUTexture &texture, MdGPUTextureBuilder &tex_builder)
{
    int w = 0, h = 0, bpp = 0;
    stbi_uc *img = stbi_load(path.c_str(), &w, &h, &bpp, 4);
    if (img == NULL)
    {
        LOG_ERROR("failed to load image");
        return MD_ERROR_UNKNOWN;
    }
    
    u64 size = w*h*bpp;
    printf("image_size: %zu\n", size);
    
    mdCreateTextureBuilder2D(tex_builder, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, bpp);
    mdSetTextureUsage(tex_builder, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mdSetFilterWrap(tex_builder, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    mdSetMipmapOptions(tex_builder, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    mdSetMagFilters(tex_builder, VK_FILTER_LINEAR, VK_FILTER_LINEAR);
    VkResult result = mdBuildTexture2D(*renderer.context, tex_builder, p_renderer_state->allocator, texture, img);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("failed to build texture");
        free(img);
        return MD_ERROR_UNKNOWN;
    }

    free(img);
    return MD_SUCCESS;
}

MdResult mdLoadOBJFromPath(MdRenderer &renderer, const std::string& path, MdGPUBuffer &vertex_buffer, MdGPUBuffer &index_buffer, usize *geometry_size)
{
    float *geometry;
    mdLoadOBJ(path.c_str(), &geometry, geometry_size);

    VkResult result = mdAllocateGPUBuffer(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
        *geometry_size*VERTEX_SIZE*sizeof(f32), 
        p_renderer_state->allocator, 
        vertex_buffer
    );
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate GPU memory for geometry");
        free(geometry);
        return MD_SUCCESS;
    }

    result = mdUploadToGPUBuffer(
        *renderer.context, 
        p_renderer_state->allocator, 
        0, 
        *geometry_size*VERTEX_SIZE*sizeof(f32),
        geometry, 
        vertex_buffer
    );
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate GPU memory for geometry");
        free(geometry);
        return MD_SUCCESS;
    }
    
    free(geometry);
    return MD_SUCCESS;
}

void mdDestroyModel(MdRenderer &renderer, MdModel &model);
MdResult mdLoadOBJModelFromPath(MdRenderer &renderer, const std::string& obj_path, MdModel &model, const std::string& tex_path = "")
{
    MdResult result = mdLoadOBJFromPath(
        renderer, 
        obj_path.c_str(), 
        model.vertex_buffer, 
        model.index_buffer, 
        &model.geometry_size
    );
    if (result != MD_SUCCESS)
    {
        LOG_ERROR("failed to load model from path \"%s\"", obj_path.c_str());
        return result;
    }

    if (tex_path == "")
        return result;

    result =  mdLoadTextureFromPath(renderer, tex_path, model.texture, model.texture_builder);
    if (result != MD_SUCCESS)
    {
        LOG_ERROR("failed to load texture from path \"%s\"", tex_path.c_str());
        mdDestroyModel(renderer, model);
        return result;
    }

    return result;
}

void mdDestroyModel(MdRenderer &renderer, MdModel &model)
{
    mdDestroyTexture(p_renderer_state->allocator, model.texture);
    mdFreeGPUBuffer(p_renderer_state->allocator, model.vertex_buffer);
    mdFreeGPUBuffer(p_renderer_state->allocator, model.index_buffer);
}

MdWindowEvent window_event = {};
int main()
{
    MdRenderer renderer;
    MdResult result = mdCreateRenderer(1920, 1080, "Midori Engine", renderer);
    if (result != MD_SUCCESS)
    {
        LOG_ERROR("failed to create renderer");
        mdDestroyRenderer(renderer);
        return -1;
    }
    VkResult vk_result;
    
    mdGetRenderState(&p_renderer_state);

    // Fences
    VkSemaphore image_available, render_finished;
    VkFence in_flight;
    {
        VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    
        semaphore_info.flags = 0;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
        vkCreateSemaphore(renderer.context->device, &semaphore_info, NULL, &image_available);
        vkCreateSemaphore(renderer.context->device, &semaphore_info, NULL, &render_finished);
        vkCreateFence(renderer.context->device, &fence_info, NULL, &in_flight);
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

    // Shadow pass
    VkDeviceSize offsets[1] = {0};
    mdCreateShadowPass(renderer);
    mdCreateGeometryPass(renderer);
    mdCreateFinalPass(renderer);

    mdBuildRenderGraph();

    MdGPUTexture *color_attachment;
    MdGPUTexture *shadow_texture;
    mdGetAttachmentTexture("color_tex1", &color_attachment);
    mdGetAttachmentTexture("shadow_map", &shadow_texture);
    
    // Load texture
    MdModel teapot = {};
    result = mdLoadOBJModelFromPath(
        renderer, 
        "../models/teapot/teapot_smooth.obj", 
        teapot, 
        "../images/test.png"
    );
    if (result != MD_SUCCESS)
        EXIT(renderer);
    
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

        mdAllocateGPUUniformBuffer(sizeof(ubo), p_renderer_state->allocator, uniform_buffer);
        mdUploadToUniformBuffer(*renderer.context, p_renderer_state->allocator, 0, sizeof(ubo), &ubo, uniform_buffer);
    }
    
    // Geometry pass pipelines    
    MdMaterial geometry_mat = {}, final_mat = {};
    MdPipeline geometry_pipeline;
    {   
        MdPipelineGeometryInputState geometry_state = {}; 
        MdPipelineRasterizationState raster_state = {};
        MdPipelineColorBlendState color_blend_state = {};
        
        // Shaders
        MdShaderSource source;
        vk_result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_vert.spv", VK_SHADER_STAGE_VERTEX_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load vertex shader");
            EXIT(renderer);
        }

        vk_result = mdLoadShaderSPIRVFromFile(*renderer.context, "../shaders/spv/test_frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT, source);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to load fragment shader");
            EXIT(renderer);
        }

        mdShaderAddBinding(source, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &teapot.texture.sampler);
        mdShaderAddBinding(source, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &color_attachment->sampler);
        mdShaderAddBinding(source, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &shadow_texture->sampler);

        mdInitGeometryInputState(geometry_state);
        mdGeometryInputAddVertexBinding(geometry_state, VK_VERTEX_INPUT_RATE_VERTEX, 8*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state, 0, 0, 3, MD_F32, 0);
        mdGeometryInputAddAttribute(geometry_state, 0, 1, 3, MD_F32, 3*sizeof(f32));
        mdGeometryInputAddAttribute(geometry_state, 0, 2, 2, MD_F32, 6*sizeof(f32));
        mdBuildGeometryInputState(geometry_state);

        mdBuildDefaultRasterizationState(raster_state);
        mdBuildDefaultColorBlendState(color_blend_state);

        vk_result = mdCreateGraphicsPipeline(
            renderer,
            source,
            &geometry_state,
            &raster_state,
            &color_blend_state,
            "geometry",
            geometry_pipeline
        );
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics pipeline");
            EXIT(renderer);
        }

        vk_result = mdCreateMaterial(renderer, geometry_pipeline, geometry_mat);
        if (vk_result != VK_SUCCESS)
        {
            LOG_ERROR("failed to create graphics material");
            EXIT(renderer);
        }
        
        mdDestroyShaderSource(*renderer.context, source);
    }

    // Shadow and final pass pipelines
    mdCreateShadowPassPipeline(renderer);
    mdCreateFinalPassPipeline(renderer, color_attachment, shadow_texture, final_mat);

    // Write descriptors
    mdDescriptorSetWriteImage(renderer, geometry_mat.set, 0, teapot.texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mdDescriptorSetWriteImage(renderer, geometry_mat.set, 1, *shadow_texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    mdDescriptorSetWriteImage(renderer, final_mat.set, 0, *color_attachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Set render functions
    mdAddRenderPassFunction("shadow", [=](VkCommandBuffer cmd, VkFramebuffer fb){
        vkCmdSetViewport(cmd, 0, 1, &shadow_viewport);
        vkCmdSetScissor(cmd, 0, 1, &shadow_scissor);
        VkDescriptorSet sets[] = {
            p_renderer_state->global_set,
            p_renderer_state->camera_sets[0]
        };
        usize sets_count = sizeof(sets) / sizeof(VkDescriptorSet);

        vkCmdBindVertexBuffers(
            cmd, 
            0, 
            1, 
            &teapot.vertex_buffer.buffer, 
            offsets
        );
        vkCmdBindDescriptorSets(
            cmd, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            p_renderer_state->shadow_pipeline.layout, 
            0, 
            sets_count, 
            sets, 
            0, 
            NULL
        );
        vkCmdBindPipeline(
            cmd, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            p_renderer_state->shadow_pipeline.pipeline
        );
        vkCmdDraw(cmd, teapot.geometry_size, 1, 0, 0);
    });

    mdAddRenderPassFunction("geometry", [=](VkCommandBuffer cmd, VkFramebuffer fb){
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        VkDescriptorSet sets[] = {
            p_renderer_state->global_set,
            p_renderer_state->camera_sets[0],
            geometry_mat.set
        };
        usize sets_count = sizeof(sets) / sizeof(VkDescriptorSet);
        
        vkCmdBindVertexBuffers(
            cmd, 
            0, 
            1, 
            &teapot.vertex_buffer.buffer, offsets
        );
        vkCmdBindDescriptorSets(
            cmd, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            geometry_pipeline.layout, 
            0, 
            sets_count, 
            sets, 
            0, 
            NULL
        );
        vkCmdBindPipeline(
            cmd, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            geometry_pipeline.pipeline
        );
        vkCmdDraw(cmd, teapot.geometry_size, 1, 0, 0);
    });

    mdAddRenderPassFunction("final", [=](VkCommandBuffer cmd, VkFramebuffer fb){
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        VkDescriptorSet sets[] = {
            p_renderer_state->global_set,
            p_renderer_state->camera_sets[0],
            final_mat.set
        };
        usize sets_count = sizeof(sets) / sizeof(VkDescriptorSet);

        vkCmdBindDescriptorSets(
            cmd, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            p_renderer_state->final_pipeline.layout, 
            0, 
            sets_count, 
            sets, 
            0, 
            NULL
        );
        vkCmdBindPipeline(
            cmd, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            p_renderer_state->final_pipeline.pipeline
        );
        vkCmdDraw(cmd, 3, 1, 0, 0);
    });

    window_event.event = MD_WINDOW_UNCHANGED;

    mdWindowRegisterWindowResizedCallback(renderer.window, [](u16 w, u16 h){
        window_event.event = MD_WINDOW_RESIZED;
        window_event.nw = w;
        window_event.nh = h;
    });
    std::vector<VkClearValue> depth_values;
    depth_values.push_back({.8, .8, .8, 1.});
    depth_values.push_back({.depthStencil = {1.0f, 0}});

    std::vector<VkClearValue> values(1);
    values.push_back({.8, .8, .8, 1.});

    std::vector<VkCommandBuffer> pass_buffers;

    do 
    {
        if (window_event.event == MD_WINDOW_RESIZED)
        {
            vkDeviceWaitIdle(renderer.context->device);
            // TO-DO: Rebuild Swapchain
        }
        vkWaitForFences(renderer.context->device, 1, &in_flight, VK_TRUE, UINT64_MAX);
        
        if (max_frames > -1 && frame_count++ >= max_frames)
            break;

        vk_result = vkAcquireNextImageKHR(
            renderer.context->device, 
            renderer.context->swapchain, 
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

        vkResetFences(renderer.context->device, 1, &in_flight);
        mdPrimeRenderGraph();

        // Update descriptors
        {
            ubo.u_time = mdGetTicks() / 1000.0f;
            mdUploadToUniformBuffer(*renderer.context, p_renderer_state->allocator, 0, sizeof(ubo), &ubo, uniform_buffer);
            mdDescriptorSetWriteUBO(
                renderer, 
                p_renderer_state->global_set, 
                0, 
                0, 
                sizeof(ubo), 
                uniform_buffer
            );
        }

        // Command recording
        mdExecuteRenderPass(depth_values, 0, image_index);
        mdExecuteRenderPass(depth_values, 1);
        mdExecuteRenderPass(depth_values, 2);
        mdRenderGraphSubmit(pass_buffers, image_index);

        // Submit to queue and present image
        {
            VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit_info.commandBufferCount = pass_buffers.size();
            submit_info.pCommandBuffers = pass_buffers.data();
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &image_available;
            submit_info.pWaitDstStageMask = &wait_stages;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &render_finished;
            vk_result = vkQueueSubmit(p_renderer_state->graphics_queue.queue_handle, 1, &submit_info, in_flight);
            if (vk_result != VK_SUCCESS)
                break;
            
            VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &renderer.context->swapchain.swapchain;
            present_info.pImageIndices = &image_index;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &render_finished;
            vk_result = vkQueuePresentKHR(p_renderer_state->graphics_queue.queue_handle, &present_info); 
            if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                window_event.event = MD_WINDOW_RESIZED;
            }
        }
        
        mdPollEvent(renderer.window);
    }
    while(!mdWindowShouldClose(renderer.window));

    vkQueueWaitIdle(p_renderer_state->graphics_queue.queue_handle);

    // Destroy materials and pipelines
    mdDestroyPipeline(renderer, geometry_pipeline);
    mdDestroyPipeline(renderer, p_renderer_state->final_pipeline);
    mdDestroyPipeline(renderer, p_renderer_state->shadow_pipeline);
    mdDestroyDescriptorAllocator();

    // Destroy render graph
    mdRenderGraphDestroy();

    // Destroy fences and semaphores
    vkDestroySemaphore(renderer.context->device, image_available, NULL);
    vkDestroySemaphore(renderer.context->device, render_finished, NULL);
    vkDestroyFence(renderer.context->device, in_flight, NULL);

    // Destroy Model
    mdDestroyModel(renderer, teapot);

    // Free GPU memory
    mdFreeGPUBuffer(p_renderer_state->allocator, uniform_buffer);
    
    mdDestroyRenderer(renderer);
    return 0;
}