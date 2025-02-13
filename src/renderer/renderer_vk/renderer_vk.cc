#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#define MD_USE_VULKAN
#include <renderer.h>
#include <platform/file/file.h>
#include <platform/window/window.h>
#include <simd_math.h>

#include <renderer_vk/renderer_vk_utils.h>

#include <vector>
#include <map>

/*
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
*/

MdRenderState renderer_state;

void mdGetRenderState(MdRenderState **pp_state) { *pp_state = &renderer_state; }

#pragma region [ Render Graph ]

u32 get_bit_count_8(u8 b)
{
    static const u8 size_arr_u4[16] = {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
    return size_arr_u4[b & 0xF] + size_arr_u4[b >> 4];
}

u32 get_bit_count_64(u64 w)
{
    u32 count = 0;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w); w >>= 8;
    count += get_bit_count_8(w);
    return count;
}

enum MdNodeStatus
{
    MD_NODE_AVAILABLE,
    MD_NODE_USED
};

#include <functional>

struct MdRenderPassAttachment
{
    bool swapchain_attachment = false;
    MdRenderPassAttachmentType  type;
    MdGPUTextureBuilder         builder;
    MdGPUTexture                texture;
};

struct MdRenderPassAttachmentBarrier
{
    MdRenderPassAttachmentType type;
};

struct MdRenderPassEntry
{
    bool is_swapchain_output = false;
    MdNodeStatus status;
    std::string id;

    VkRenderPass pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkClearValue> clear_values;

    std::function<void(VkCommandBuffer, VkFramebuffer)> record = NULL;

    std::vector<std::string> input_attachments;
    std::vector<std::string> output_attachments;
    VkCommandBuffer buffer = VK_NULL_HANDLE;
};

struct MdRenderGraphNode
{
    MdNodeStatus status;
    u32 stage;
    u32 index;
};

struct MdAdjacencyMatrix
{
    std::array<u64, 64> matrix;
    bool GetBit(u32 i, u32 j);
    void SetBit(u32 i, u32 j, bool set);
};

bool MdAdjacencyMatrix::GetBit(u32 i, u32 j)            { return (matrix[j] & (1<<i)) != 0; }
void MdAdjacencyMatrix::SetBit(u32 i, u32 j, bool set)  { matrix[j] = (set) ? (matrix[j] | (1<<i)) : (matrix[j] & ~(1<<i)); }

struct MdRenderGraph
{
    std::array<MdRenderPassEntry, 64> passes;
    std::array<MdRenderGraphNode, 64> nodes;
    std::array<MdRenderGraphNode, 64> compiled_nodes;
    bool build_buffers = true;

    usize compiled_count, node_count, pass_count;
    MdAdjacencyMatrix adj_matrix;

    VkDevice device;
    MdRenderContext *p_context;

    VkCommandPool pool = VK_NULL_HANDLE;
};
MdRenderGraph render_graph;

struct MdAttachmentList
{
    std::map<std::string, MdRenderPassAttachmentBarrier> barriers; 
    std::map<std::string, MdRenderPassAttachment> attachments;
};
MdAttachmentList attachment_list;

VkResult mdAddAttachment(const std::string &name, MdRenderPassAttachmentInfo &info)
{
    MdRenderPassAttachment att = {};
    att.type = info.type;
    att.swapchain_attachment = info.is_swapchain;

    // TO-DO: make it so that the user can define their own resolution, rather than setting it to that of the swapchain
    info.width = render_graph.p_context->swapchain.extent.width;
    info.height = render_graph.p_context->swapchain.extent.height;
    
    // Check if we need to make a new attachment
    auto att_iter = attachment_list.attachments.find(name);
    bool dirty_texture = false;
    if (att_iter == attachment_list.attachments.end())
    {
        dirty_texture = true;
        attachment_list.attachments.insert(std::pair(name, att));
        att_iter = attachment_list.attachments.find(name);
    }
    else if(att_iter->second.type != info.type)
    {
        dirty_texture = true;
        att_iter->second.builder = {};
        mdDestroyTexture(renderer_state.allocator, att_iter->second.texture);
    }

    // If the texture is marked as 'dirty', make a new one
    if (dirty_texture)
    {
        VkResult result = VK_ERROR_UNKNOWN;
        switch (info.type)
        {
            case MD_ATTACHMENT_TYPE_COLOR:
                mdCreateTextureBuilder2D(att_iter->second.builder, info.width, info.height, info.format, VK_IMAGE_ASPECT_COLOR_BIT);
                mdSetTextureUsage(att_iter->second.builder, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
                mdSetFilterWrap(att_iter->second.builder, info.address[0], info.address[1], info.address[2]);
                mdSetTextureBorderColor(att_iter->second.builder, info.border_color);
                result = mdBuildColorAttachmentTexture2D(*render_graph.p_context, att_iter->second.builder, renderer_state.allocator, att_iter->second.texture);
                break;
            case MD_ATTACHMENT_TYPE_DEPTH: 
                mdCreateTextureBuilder2D(att_iter->second.builder, info.width, info.height, info.format, VK_IMAGE_ASPECT_DEPTH_BIT);
                mdSetTextureUsage(att_iter->second.builder, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
                mdSetFilterWrap(att_iter->second.builder, info.address[0], info.address[1], info.address[2]);
                mdSetTextureBorderColor(att_iter->second.builder, info.border_color);
                result = mdBuildDepthAttachmentTexture2D(*render_graph.p_context, att_iter->second.builder, renderer_state.allocator, att_iter->second.texture);
                break;
        }
    }

    return VK_SUCCESS;
}

void mdGetAttachmentTexture(const std::string &name, MdGPUTexture **pp_texture)
{
    auto att_it = attachment_list.attachments.find(name);
    if (att_it == attachment_list.attachments.end())
    {
        LOG_ERROR("failed to find attachment with id \"%s\"", name.c_str());
        return;
    }

    *pp_texture = &att_it->second.texture;
    return;
}

void mdAddAttachmentBarrier(const std::string &name)
{
    auto att_it = attachment_list.attachments.find(name);
    if (att_it == attachment_list.attachments.end())
    {
        LOG_ERROR("failed to find attachment with name \"%s\"", name.c_str());
        return;
    }

    auto it = attachment_list.barriers.find(name);
    if (it == attachment_list.barriers.end())
    {
        attachment_list.barriers.insert(std::pair<std::string, MdRenderPassAttachmentBarrier>(
            name, {}
        ));
        it = attachment_list.barriers.find(name);
    }

    it->second.type = att_it->second.type;
}

void mdFlushAttachments()
{
    auto ptr = &attachment_list.attachments;
    for (auto it=ptr->begin(); it!=ptr->end(); it++)
        mdDestroyTexture(renderer_state.allocator, it->second.texture);

    ptr->clear();
}

void mdRenderGraphInit(MdRenderContext *p_context)
{
    render_graph.node_count = 0;
    render_graph.pass_count = 0;

    for (u32 i=0; i<render_graph.passes.size(); i++)
    {
        render_graph.passes[i].status = MD_NODE_AVAILABLE;
        render_graph.nodes[i].status = MD_NODE_AVAILABLE;
        render_graph.adj_matrix.matrix[i] = 0;
    }
    render_graph.p_context = p_context;
    render_graph.device = p_context->device;
}

void mdRenderGraphDestroy()
{
    std::vector<VkCommandBuffer> buffers;
    buffers.reserve(render_graph.compiled_count);
    for (u32 i=0; i<render_graph.compiled_count; i++)
    {
        u32 pass_index = render_graph.compiled_nodes[i].index;
        buffers.push_back(render_graph.passes[pass_index].buffer);
    }

    vkFreeCommandBuffers(
        render_graph.device, 
        render_graph.pool, 
        buffers.size(), 
        buffers.data()
    );
    vkDestroyCommandPool(render_graph.device, render_graph.pool, NULL);

    mdFlushAttachments();
    for (u32 i=0; i<render_graph.passes.size(); i++)
    {
        if (render_graph.passes[i].pass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(render_graph.device, render_graph.passes[i].pass, NULL);
            render_graph.passes[i].pass = VK_NULL_HANDLE;
        }
    }
}

void mdRenderGraphClear()
{
    render_graph.node_count = 0;
    for (u32 i=0; i<render_graph.passes.size(); i++)
    {
        render_graph.nodes[i].status = MD_NODE_AVAILABLE;
        render_graph.adj_matrix.matrix[i] = 0;
    }
}

u32 mdFindRenderPass(const std::string& id)
{ 
    for (u32 i=0; i<render_graph.passes.size(); i++){
        if (render_graph.passes[i].status == MD_NODE_USED && 
            render_graph.passes[i].id == id)
            return i;
    }
    return UINT32_MAX; 
}

void mdAddRenderPass(const std::string& id, bool is_swapchain)
{
    u32 idx = mdFindRenderPass(id);
    if (idx != UINT32_MAX)
    {
        LOG_ERROR("pass \"%s\" already exists", id.c_str());
        return;
    }

    for (u32 i=0; i<render_graph.passes.size(); i++)
    {
        if (render_graph.passes[i].status != MD_NODE_AVAILABLE)
            continue;
        
        render_graph.passes[i].is_swapchain_output = is_swapchain;
        render_graph.passes[i].status = MD_NODE_USED;
        render_graph.passes[i].id = id;
        render_graph.passes[i].input_attachments.clear();
        render_graph.passes[i].output_attachments.clear();
        printf("added pass %s\n", id.c_str());
        render_graph.pass_count++;

        return;
    }

    LOG_ERROR("failed to add pass \"%s\"", id.c_str());
    return;
}

VkResult mdAddRenderPassInput(  const std::string& id, 
                                const std::string& input, 
                                MdRenderPassAttachmentInfo &info)
{
    u32 idx = mdFindRenderPass(id);
    if (idx == UINT32_MAX)
    {
        LOG_ERROR("failed to find pass with id \"%s\"", id.c_str());
        return VK_ERROR_UNKNOWN;
    }

    MdRenderPassEntry *p_entry = &render_graph.passes[idx];
    for (auto it=p_entry->input_attachments.begin(); it!=p_entry->input_attachments.end(); it++)
    {
        if (*it != input)
            continue;
        
        LOG_ERROR("pass \"%s\" already has an input attachment \"%s\"", id.c_str(), input.c_str());
        return VK_ERROR_UNKNOWN;
    }

    VkResult result = mdAddAttachment(input, info);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("failed to create attachment for input \"%s\"", input.c_str());
        return result;
    }

    p_entry->input_attachments.push_back(input);
    return result;
}

VkResult mdAddRenderPassOutput( const std::string& id, 
                                const std::string& output, 
                                MdRenderPassAttachmentInfo &info)
{
    u32 idx = mdFindRenderPass(id);
    if (idx == UINT32_MAX)
    {
        LOG_ERROR("failed to find pass with id \"%s\"", id.c_str());
        return VK_ERROR_UNKNOWN;
    }

    MdRenderPassEntry *p_entry = &render_graph.passes[idx];
    for (auto it=p_entry->output_attachments.begin(); it!=p_entry->output_attachments.end(); it++)
    {
        if (*it != output)
            continue;
        
        LOG_ERROR("pass \"%s\" already has an output attachment \"%s\"", id.c_str(), output.c_str());
        return VK_ERROR_UNKNOWN;
    }

    VkResult result = mdAddAttachment(output, info);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("failed to create attachment for input \"%s\"", output.c_str());
        return result;
    }

    p_entry->output_attachments.push_back(output);
    return result;
}

VkResult mdAddRenderPassInput(  const std::string& id, 
                                const std::string& input)
{
    u32 idx = mdFindRenderPass(id);
    if (idx == UINT32_MAX)
    {
        LOG_ERROR("failed to find pass with id \"%s\"", id.c_str());
        return VK_ERROR_UNKNOWN;
    }

    MdRenderPassEntry *p_entry = &render_graph.passes[idx];
    for (auto it=p_entry->input_attachments.begin(); it!=p_entry->input_attachments.end(); it++)
    {
        if (*it != input)
            continue;
        
        LOG_ERROR("pass \"%s\" already has an output attachment \"%s\"", id.c_str(), input.c_str());
        return VK_ERROR_UNKNOWN;
    }

    auto att_it = attachment_list.attachments.find(input);
    if (att_it == attachment_list.attachments.end())
    {
        LOG_ERROR("attachment \"%s\" doesn't exist", input.c_str());
        return VK_ERROR_UNKNOWN;
    }

    p_entry->input_attachments.push_back(input);
    return VK_SUCCESS;
}

VkResult mdAddRenderPassOutput( const std::string& id, 
                                const std::string& output)
{
    u32 idx = mdFindRenderPass(id);
    if (idx == UINT32_MAX)
    {
        LOG_ERROR("failed to find pass with id \"%s\"", id.c_str());
        return VK_ERROR_UNKNOWN;
    }

    MdRenderPassEntry *p_entry = &render_graph.passes[idx];
    for (auto it=p_entry->output_attachments.begin(); it!=p_entry->output_attachments.end(); it++)
    {
        if (*it != output)
            continue;
        
        LOG_ERROR("pass \"%s\" already has an output attachment \"%s\"", id.c_str(), output.c_str());
        return VK_ERROR_UNKNOWN;
    }
    
    auto att_it = attachment_list.attachments.find(output);
    if (att_it == attachment_list.attachments.end())
    {
        LOG_ERROR("attachment \"%s\" doesn't exist", output.c_str());
        return VK_ERROR_UNKNOWN;
    }
    
    p_entry->output_attachments.push_back(output);
    return VK_SUCCESS;
}


void mdAddRenderPassFunction(const std::string& id, const std::function<void(VkCommandBuffer, VkFramebuffer)> &func)
{
    u32 idx = mdFindRenderPass(id);
    if (idx == UINT32_MAX)
    {
        LOG_ERROR("failed to find pass with id \"%s\"", id.c_str());
        return;
    }

    MdRenderPassEntry *p_entry = &render_graph.passes[idx];
    p_entry->record = func;
}

u64 mdRenderGraphFindNode(u64 id)
{
    for (u32 i=0; i<render_graph.nodes.size(); i++)
        if (render_graph.nodes[i].status == MD_NODE_USED && 
            render_graph.nodes[i].index == id)
            return i;
    return UINT64_MAX;
}

void mdRenderGraphAddNode(u64 id)
{
    if (render_graph.node_count >= render_graph.nodes.size())
    {
        LOG_ERROR("render graph is full");
        return;
    }

    u64 idx = mdRenderGraphFindNode(id);
    if (idx != UINT64_MAX)
    {
        LOG_ERROR("node with id \"%d\" already exists", id);
        return;
    }

    for (u32 i=0; i<render_graph.nodes.size(); i++)
    {
        if (render_graph.nodes[i].status != MD_NODE_AVAILABLE)
            continue;
        
        idx = i;
        break;
    }

    if (idx == UINT64_MAX)
    {
        LOG_ERROR("render graph is full");
        return;
    }

    render_graph.nodes[idx].status = MD_NODE_USED;
    render_graph.nodes[idx].index = id;
    render_graph.node_count++;
}

void mdRenderGraphAddEdge(u64 start, u64 end)
{
    if (start == end)
    {
        LOG_ERROR("start must not equal end");
        return;
    }
    
    u64 start_idx = mdRenderGraphFindNode(start);
    if (start_idx == UINT64_MAX)
    {
        LOG_ERROR("failed to find node \"%ld\"", start);
        return;
    }
    
    u64 end_idx = mdRenderGraphFindNode(end);
    if (end_idx == UINT64_MAX)
    {
        LOG_ERROR("failed to find node \"%ld\"", end);
        return;
    }

    render_graph.adj_matrix.SetBit(start_idx, end_idx, true);
}

void mdRenderGraphTopologicalSort()
{
    u64 removed = UINT64_MAX << render_graph.node_count;
    u32 in_degrees[64] = {0};

    u32 stage = 0;
    bool found = 0;

    // "Clear" the list of compiled nodes
    render_graph.compiled_count = 0;

    MdAdjacencyMatrix *p_matrix = &render_graph.adj_matrix;
    for (u32 i=0; i<render_graph.node_count; i++)
    {
        in_degrees[i] = get_bit_count_64(p_matrix->matrix[i]);
        found |= (in_degrees[i] == 0);
    }

    if (!found)
    {
        LOG_ERROR("this graph should not have any cycles");
        return;
    }

    do
    {
        u64 marked = UINT64_MAX << render_graph.node_count;
        found = false;

        for (u64 i=0; i<render_graph.node_count; i++)
        {
            if (in_degrees[i] != 0 || ((removed & (1<<i)) != 0))
                continue;
            
            found = true;
            marked |= (1<<i);

            render_graph.compiled_nodes[render_graph.compiled_count].stage = stage;
            render_graph.compiled_nodes[render_graph.compiled_count++].index = render_graph.nodes[i].index;
        }

        if (!found) break;
        for (u64 i=0; i<render_graph.node_count; i++)
        {
            if ((marked & (1<<i)) == 0)
                continue;
            
            for (u32 c=0; c<render_graph.node_count; c++)
            {
                if ((p_matrix->matrix[c] & (1<<i)) == 0)
                    continue;
                
                in_degrees[c]--;
                if (in_degrees[c] == UINT32_MAX)
                {
                    LOG_ERROR("this graph should not have any cycles");
                    return;
                }
            }
        }
        removed |= marked;
        stage++;
    }
    while (1);
}

VkResult mdRenderGraphBuildPass(u32 index)
{
    std::array<VkSubpassDependency, 4> subpasses = {};
    u8 subpass_count = 0;

    VkAttachmentDescription attachments[2] = {};
    VkAttachmentReference att_refs[2] = {};
    VkSubpassDependency deps[2] = {};
    
    VkSubpassDescription desc = {};
    
    usize att_count = 0;

    // If the pass is a swapchain pass, just build the pass with one color attachment
    if (render_graph.passes[index].is_swapchain_output)
    {
        attachments[0].flags = 0;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[0].format = render_graph.p_context->swapchain.image_format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        att_refs[0].attachment = att_count;
        att_refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        deps[0].dependencyFlags = 0;
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = 0;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
        desc.colorAttachmentCount = 1;
        desc.pColorAttachments = &att_refs[0];
        
        VkRenderPassCreateInfo pass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &desc;
        pass_info.attachmentCount = 1;
        pass_info.pAttachments = attachments;
        pass_info.dependencyCount = 1;
        pass_info.pDependencies = deps;
        pass_info.flags = 0;

        return vkCreateRenderPass(
            render_graph.device, 
            &pass_info, 
            NULL, 
            &render_graph.passes[index].pass
        );
    }

    auto ptr = &render_graph.passes[index].output_attachments;
    bool        has_color = false, 
                has_depth = false;
    VkFormat    color_format = VK_FORMAT_UNDEFINED, 
                depth_format = VK_FORMAT_UNDEFINED;

    for (auto it=ptr->begin(); it!=ptr->end(); it++)
    {
        auto att_it = attachment_list.attachments.find(*it);
        if (att_it == attachment_list.attachments.end())
            continue;

        has_color |= (att_it->second.type == MD_ATTACHMENT_TYPE_COLOR);
        has_depth |= (att_it->second.type == MD_ATTACHMENT_TYPE_DEPTH);

        if (has_color) color_format = att_it->second.builder.image_info.format;
        if (has_depth) depth_format = att_it->second.builder.image_info.format;
    }

    // Setup color attachment descriptions, refs, and subpass dependencies
    if (has_color)
    {
        attachments[att_count].flags = 0;
        attachments[att_count].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[att_count].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[att_count].format = color_format;
        attachments[att_count].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[att_count].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[att_count].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[att_count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[att_count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        att_refs[att_count].attachment = att_count;
        att_refs[att_count].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        deps[att_count].dependencyFlags = 0;
        deps[att_count].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[att_count].dstSubpass = 0;
        deps[att_count].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[att_count].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[att_count].srcAccessMask = 0;
        deps[att_count].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
        desc.colorAttachmentCount = 1;
        desc.pColorAttachments = &att_refs[att_count];
        
        att_count++;
    }
    
    // Setup depth attachment descriptions, refs, and subpass dependencies
    if (has_depth)
    {
        attachments[att_count].flags = 0;
        attachments[att_count].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[att_count].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[att_count].format = depth_format;
        attachments[att_count].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[att_count].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[att_count].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[att_count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[att_count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        att_refs[att_count].attachment = att_count;
        att_refs[att_count].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        deps[att_count].dependencyFlags = 0;
        deps[att_count].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[att_count].dstSubpass = 0;
        deps[att_count].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[att_count].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[att_count].srcAccessMask = 0;
        deps[att_count].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        desc.pDepthStencilAttachment = &att_refs[att_count];

        att_count++;
    }

    desc.preserveAttachmentCount = 0;
    desc.pPreserveAttachments = NULL;
    desc.pResolveAttachments = NULL;
    desc.inputAttachmentCount = 0;
    desc.pInputAttachments = NULL;
    desc.flags = 0;
    desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    VkRenderPassCreateInfo pass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &desc;
    pass_info.attachmentCount = att_count;
    pass_info.pAttachments = attachments;
    pass_info.dependencyCount = att_count;
    pass_info.pDependencies = deps;
    pass_info.flags = 0;

    VkResult result = vkCreateRenderPass(
        render_graph.device, 
        &pass_info, 
        NULL, 
        &render_graph.passes[index].pass
    );
    printf("built pass \"%s\"\n", render_graph.passes[index].id.c_str());
    return result;
}

void mdRenderGraphClearFramebuffers()
{
    for (usize p=0; p<render_graph.pass_count; p++)
    {
        auto pass_ptr = &render_graph.passes[p];
        for (usize o=0; o<pass_ptr->output_attachments.size(); o++)
        {
            auto att_ptr = &attachment_list.attachments;
            auto att_it = att_ptr->find(pass_ptr->output_attachments[o]);
            if (att_it == att_ptr->end())
            {
                LOG_ERROR("Attachment \"%s\" not found for pass \"%s\"", 
                    pass_ptr->output_attachments[o].c_str(),
                    pass_ptr->id.c_str()
                );
                return;
            }

            for (usize fb=0; fb<pass_ptr->framebuffers.size(); fb++)
            {
                if (pass_ptr->framebuffers[fb] == VK_NULL_HANDLE)
                    continue;
                
                vkDestroyFramebuffer(render_graph.device, pass_ptr->framebuffers[fb], NULL);
                pass_ptr->framebuffers[fb] = NULL;
            }
        }
    }
}

// TO-DO: make it so that the user can render to their own target texture, and 
// don't assume that the width and height of a buffer will always be that of
// the swapchain.    
VkResult mdRenderGraphGenerateFramebuffers(const std::vector<VkImageView> &swapchain_images)
{
    VkFramebufferCreateInfo fb_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.layers = 1;
    fb_info.width = render_graph.p_context->swapchain.extent.width;
    fb_info.height = render_graph.p_context->swapchain.extent.height;
    
    VkFramebuffer handle = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
    views.reserve(16);

    // Clear framebuffers if need be
    mdRenderGraphClearFramebuffers();

    // For every pass, look at the output attachments and add them to a list
    VkResult result = VK_SUCCESS;
    for (usize n=0; n<render_graph.compiled_count; n++)
    {
        // Get the pass index
        u32 p = render_graph.compiled_nodes[n].index;
        
        // Reset the image view list
        views.clear();
        
        // Get the current render pass
        auto pass_ptr = &render_graph.passes[p];
        fb_info.renderPass = pass_ptr->pass;
        
        if (!pass_ptr->is_swapchain_output)
        {
            // For every attachment, push back its image view
            for (int o=0; o<pass_ptr->output_attachments.size(); o++)
            {
                auto att_ptr = &attachment_list.attachments;
                auto att_it = att_ptr->find(pass_ptr->output_attachments[o]);
                if (att_it == att_ptr->end())
                {
                    LOG_ERROR("Attachment \"%s\" not found for pass \"%s\"", 
                        pass_ptr->output_attachments[o].c_str(),
                        pass_ptr->id.c_str()
                    );
                    return VK_ERROR_UNKNOWN;
                }

                // Push back VK_NULL_HANDLE since we'll set it to the swapchain's image view(s)
                views.push_back(att_it->second.texture.image_view);
            }

            // If this pass is a swapchain output, then generate a framebuffer for each swapchain image,
            // else push back only one framebuffer
            usize sw_index = 0;
            
            fb_info.attachmentCount = views.size();
            fb_info.pAttachments = views.data();        

            result = vkCreateFramebuffer(render_graph.p_context->device, &fb_info, NULL, &handle);
            VK_CHECK(result, "failed to create framebuffer");

            pass_ptr->framebuffers.push_back(handle);
        }
        else 
        {
            // For every attachment, push back its image view
            for (int o=0; o<pass_ptr->input_attachments.size(); o++)
            {
                auto att_ptr = &attachment_list.attachments;
                auto att_it = att_ptr->find(pass_ptr->input_attachments[o]);
                if (att_it == att_ptr->end())
                {
                    LOG_ERROR("Attachment \"%s\" not found for pass \"%s\"", 
                        pass_ptr->output_attachments[o].c_str(),
                        pass_ptr->id.c_str()
                    );
                    return VK_ERROR_UNKNOWN;
                }

                // Push back VK_NULL_HANDLE since we'll set it to the swapchain's image view(s)
                if (!att_it->second.swapchain_attachment)
                    views.push_back(att_it->second.texture.image_view);
                else
                    views.push_back(VK_NULL_HANDLE);
            }

            // If this pass is a swapchain output, then generate a framebuffer for each swapchain image,
            // else push back only one framebuffer
            usize sw_index = 0;
            for (usize i=0; i<views.size(); i++)
            {
                if (views[i] != VK_NULL_HANDLE) continue;

                sw_index = i;
                break;
            }

            for (usize sw_img=0; sw_img<swapchain_images.size(); sw_img++)
            {
                views[sw_index] = swapchain_images[sw_img];
                fb_info.attachmentCount = views.size();
                fb_info.pAttachments = views.data();        

                result = vkCreateFramebuffer(render_graph.p_context->device, &fb_info, NULL, &handle);
                VK_CHECK(result, "failed to create framebuffer");

                pass_ptr->framebuffers.push_back(handle);
            }
        }
    }

    return result;
}

void mdBuildRenderGraphEdges(u32 index, u64 *p_visited)
{
    *p_visited |= 1 << index;

    // For a given input, find all nodes with a corresponding output
    MdRenderPassEntry *p_end = &render_graph.passes[index];
    auto end_it = p_end->input_attachments.begin();

    for (u32 i=0; i<p_end->input_attachments.size(); i++)
    {
        for (u32 p=0; p<render_graph.passes.size(); p++)
        {   
            bool visited = (*p_visited & (1 << p)) != 0;
            if (visited || render_graph.passes[p].status != MD_NODE_USED)
                continue;
            
            //printf("index: %d\n", index);
            MdRenderPassEntry *p_start = &render_graph.passes[p];
            auto start_it = p_start->output_attachments.begin();
            
            for (u32 o=0; o<p_start->output_attachments.size(); o++)
            {
                if (start_it == p_start->output_attachments.end() || 
                    end_it == p_end->input_attachments.end()) 
                    break;

                if (*start_it != *end_it)
                    continue;
                
                mdRenderGraphAddNode(p);
                mdRenderGraphAddEdge(index, p);

                // Insert barrier here
                mdAddAttachmentBarrier(*end_it);
                mdBuildRenderGraphEdges(p, p_visited);

                start_it++;
                break;
            }
        }
        end_it++;
    }
}

void mdBuildRenderGraph()
{
    if (render_graph.pass_count < 1)
    {
        LOG_ERROR("render graph must have at least one pass");
        return;
    }

    u32 index = mdFindRenderPass("final");
    if (index == UINT32_MAX)
    {
        LOG_ERROR("render graph should have a pass named \"final\"");
        return;
    }

    u64 visited = UINT64_MAX << render_graph.pass_count;
    
    // Build edges of the graph
    mdRenderGraphClear();
    mdRenderGraphAddNode(index);
    mdBuildRenderGraphEdges(index, &visited);

    // Sort the graph
    mdRenderGraphTopologicalSort();

    // Only build the render passes that are used
    for (u32 n=0; n<render_graph.compiled_count; n++)
        mdRenderGraphBuildPass(render_graph.compiled_nodes[n].index);
    
    // Generate framebuffers
    mdRenderGraphGenerateFramebuffers(render_graph.p_context->sw_image_views);
    return;
}

VkRenderPass mdRenderGraphGetPass(const std::string &pass)
{
    bool found = false;
    VkRenderPass rp = VK_NULL_HANDLE;
    for (auto it=render_graph.passes.begin(); it!=render_graph.passes.end(); it++)
    {
        if (it->id != pass) 
            continue;
        
        found = true;
        rp = it->pass;
        break;
    }
    
    if (!found)
    {
        LOG_ERROR("pass with id \"%s\" does not exist", pass.c_str());
        return rp;
    }

    if (rp == VK_NULL_HANDLE)
    {
        LOG_ERROR("pass with id \"%s\" has not been built yet", pass.c_str());
        return rp;
    }

    return rp;
}

VkResult mdPrimeRenderGraph()
{
    VkResult result = VK_SUCCESS;
    std::vector<VkCommandBuffer> buffers;

    if (render_graph.pool == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = renderer_state.graphics_queue.queue_index;

        result = vkCreateCommandPool(render_graph.device, &pool_info, NULL, &render_graph.pool);
        VK_CHECK(result, "failed to create command pool");

        if (render_graph.build_buffers)
        {
            buffers.reserve(render_graph.compiled_count);
            for (u32 i=0; i<render_graph.node_count; i++)
                buffers.push_back(VK_NULL_HANDLE);
        }
    }

    if (render_graph.build_buffers)
    {
        VkCommandBufferAllocateInfo buffer_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        buffer_info.commandBufferCount = buffers.size();
        buffer_info.commandPool = render_graph.pool;
        buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        result = vkAllocateCommandBuffers(render_graph.device, &buffer_info, buffers.data());
        VK_CHECK(result, "failed to create command buffers");

        for (u32 i=0; i<render_graph.compiled_count; i++)
        {
            u32 pass_index = render_graph.compiled_nodes[i].index;
            render_graph.passes[pass_index].buffer = buffers[i];
        }

        vkResetCommandPool(render_graph.device, render_graph.pool, 0);
    }

    render_graph.build_buffers = false;
    return result;
}

usize mdGetCommandBufferCount() { return render_graph.compiled_count; }

VkResult mdRenderGraphResetBuffers()
{
    return vkResetCommandPool(render_graph.device, render_graph.pool, 0);
}

void mdExecuteRenderPass(const std::vector<VkClearValue> &values, const std::string &pass, u32 fb_index)
{
    bool found = false;
    auto it=render_graph.passes.begin();
    u32 pass_index = 0;
    
    while (it != render_graph.passes.end())
    {
        if (it->id != pass) 
        {
            it++;
            pass_index++;
            continue;
        }
        found = true;
        break;
    }
    
    if (!found)
    {
        LOG_ERROR("pass with id \"%s\" does not exist", pass.c_str());
        return;
    }

    if (render_graph.passes[pass_index].pass == VK_NULL_HANDLE)
    {
        LOG_ERROR("pass with id \"%s\" has not been built yet", pass.c_str());
        return;
    }

    mdExecuteRenderPass(values, pass_index, fb_index);
}

void mdExecuteRenderPass(const std::vector<VkClearValue> &values, u32 pass_index, u32 fb_index)
{
    if (pass_index >= render_graph.compiled_count)
    {
        LOG_ERROR("index cannot be greater than or equal to the number of passes in the graph");
        return;
    }

    printf("Beginning pass \"%s\"\n", render_graph.passes[pass_index].id.c_str());

    // Set clear values if need be
    if (render_graph.passes[pass_index].clear_values.empty())
        render_graph.passes[pass_index].clear_values = values;

    // Get the command buffer
    VkCommandBuffer buffer = render_graph.passes[pass_index].buffer;
    VkFramebuffer fb = render_graph.passes[pass_index].framebuffers[fb_index];
    
    VkCommandBufferBeginInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    info.pInheritanceInfo = NULL;
    VkResult result = vkBeginCommandBuffer(buffer, &info);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("failed to begin command buffer for pass \"%s\"", render_graph.passes[pass_index].id.c_str());
        return;
    }

    // Insert barriers as needed
    auto input_ptr = &render_graph.passes[pass_index];
    for (u32 i=0; i<input_ptr->input_attachments.size(); i++)
    {
        // Insert the barrier if there is one for this resource
        auto it = attachment_list.barriers.find(input_ptr->input_attachments[i]);
        if (it == attachment_list.barriers.end())
            continue;

        // TO-DO: make barrier inserts more intelligent
        auto att_ptr = &attachment_list.attachments[input_ptr->input_attachments[i]];
        switch (it->second.type)
        {
            case MD_ATTACHMENT_TYPE_COLOR:
            mdTransitionImageLayout(
                att_ptr->texture, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                buffer
            );
            break;
            case MD_ATTACHMENT_TYPE_DEPTH:
            mdTransitionImageLayout(
                att_ptr->texture, 
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
                VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                buffer
            );
            break;
        }
    }
    
    // Start the render pass
    VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = render_graph.passes[pass_index].pass;
    begin_info.renderArea.offset = {0,0};
    begin_info.renderArea.extent = render_graph.p_context->swapchain.extent;
    begin_info.clearValueCount = render_graph.passes[pass_index].clear_values.size();
    begin_info.pClearValues = render_graph.passes[pass_index].clear_values.data();
    begin_info.framebuffer = fb;

    vkCmdBeginRenderPass(buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    printf("buffer: %016X\n", buffer);

    if (render_graph.passes[pass_index].record != NULL)
        render_graph.passes[pass_index].record(buffer, fb);
    
    vkCmdEndRenderPass(buffer);
    vkEndCommandBuffer(buffer);
}

void mdRenderGraphSubmit(std::vector<VkCommandBuffer> &buffers, bool refill)
{
    // Populate the buffer if need be
    if (refill)
        buffers.clear();

    if (refill || buffers.empty())
    {
        if (buffers.capacity() < render_graph.compiled_count)
            buffers.reserve(render_graph.compiled_count);

        for (u32 i=0; i<render_graph.compiled_count; i++)
        {
            u32 pass_index = render_graph.compiled_nodes[i].index;
            buffers.push_back(render_graph.passes[pass_index].buffer);
        }
    }
}

#pragma endregion

#pragma region [ Material System ]
#include <array>
#define MD_UNIFORM_POOL_BLOCK_SIZE 128
#define MD_MAX_UNIFORM_SETS 4096
enum MdDescriptorPoolStatus
{
    UNALLOCATED,
    AVAILABLE,
    FREE
};

struct MdDescriptorPool
{
    MdDescriptorPoolStatus status;
    VkDescriptorPool pool;
};

struct MdDescriptorAllocator
{
    std::vector<MdDescriptorPool> pools;
    std::array<VkDescriptorPoolSize, 4> sizes;
    std::array<std::vector<VkDescriptorSet>, 4> sets;

    u32 pool_count;
    u32 max_sets;
    VkDevice device;

    VkResult Init(VkDevice device);
    VkResult GetPool(VkDescriptorPool *p_pool);
    VkResult CreatePool(VkDescriptorPool *p_pool = VK_NULL_HANDLE);
    bool ResetPools(u32 index);
    
    VkResult AllocateSets(VkDescriptorSetLayout layout, u32 set_index, u32 size, VkDescriptorSet *p_sets);
    ~MdDescriptorAllocator();
};

VkResult MdDescriptorAllocator::Init(VkDevice device)
{
    // Initialize pools
    pools.reserve(MD_UNIFORM_POOL_BLOCK_SIZE);
    for (u32 i=0; i<MD_UNIFORM_POOL_BLOCK_SIZE; i++)
        pools.push_back({
            .status = UNALLOCATED,
            .pool = VK_NULL_HANDLE
        });

    this->max_sets = MD_MAX_UNIFORM_SETS;
    this->pool_count = 0;
    this->device = device;

    // TO-DO: Make this user configurable
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;          sizes[0].descriptorCount = 4;
    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;          sizes[1].descriptorCount = 4;
    sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;           sizes[2].descriptorCount = 4;
    sizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;  sizes[3].descriptorCount = 4;

    // Create the first pool
    return CreatePool();
}

VkResult MdDescriptorAllocator::GetPool(VkDescriptorPool *p_pool)
{
    for (u32 i=0; i<pool_count; i++)
    {
        if (pools[i].status == AVAILABLE)
        {
            *p_pool = pools[i].pool;
            return VK_SUCCESS;
        }
    }

    return CreatePool(p_pool);
}

VkResult MdDescriptorAllocator::CreatePool(VkDescriptorPool *p_pool)
{
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.poolSizeCount = sizes.size();
    pool_info.pPoolSizes = sizes.data();
    pool_info.flags = 0;
    pool_info.maxSets = max_sets;
    
    VkDescriptorPool pool;
    VkResult result = vkCreateDescriptorPool(this->device, &pool_info, NULL, &pool);
    if (result != VK_SUCCESS) return result;

    if (pool_count >= pools.size())
    {
        pools.reserve(pool_count + MD_UNIFORM_POOL_BLOCK_SIZE);
        for (u32 i=0; i<MD_UNIFORM_POOL_BLOCK_SIZE; i++)
        {
            pools.push_back({
                .status = UNALLOCATED,
                .pool = VK_NULL_HANDLE
            });
        }
    }

    pools[pool_count].status = AVAILABLE;
    pools[pool_count].pool = pool;
    pool_count++;

    if (p_pool != NULL)
        *p_pool = pool;
    
    return result;
}

bool MdDescriptorAllocator::ResetPools(u32 index)
{
    for (u32 i=0; i<pool_count; i++)
    {
        if (pools[i].status == UNALLOCATED)
            continue;
        
        VkResult result = vkResetDescriptorPool(
            this->device, 
            this->pools[i].pool, 
            0
        );
        if (result != VK_SUCCESS) return result;
    }

    return VK_SUCCESS;
}
    
VkResult MdDescriptorAllocator::AllocateSets(VkDescriptorSetLayout layout, u32 set_index, u32 size, VkDescriptorSet *p_sets)
{
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult result = GetPool(&pool);
    if (result != VK_SUCCESS) return result;

    // Allocate memory for layout and set list, because Vulkan expects an array of each
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(size);
    for (u32 i=0; i<size; i++) layouts.push_back(layout);

    // Allocate descriptor sets    
    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = size;
    alloc_info.pSetLayouts = layouts.data();

    // Allocate the descriptor sets
    result = vkAllocateDescriptorSets(this->device, &alloc_info, p_sets);
    
    // If the pool has reached capacity, make a new one
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
    {
        result = CreatePool(&pool);
        if (result != VK_SUCCESS) return result;

        alloc_info.descriptorPool = pool;
        result = vkAllocateDescriptorSets(this->device, &alloc_info, p_sets);
        
        // If we are still failing to allocate descriptor sets, then exit early
        if (result != VK_SUCCESS) return result;    
    }
    
    // If there are still errors, exit
    if (result != VK_SUCCESS)
        return result;
    
    // Push sets back and return result
    for (u32 i=0; i<size; i++)
        sets[set_index].push_back(p_sets[i]);

    return result;
}

MdDescriptorAllocator::~MdDescriptorAllocator()
{
    for (u32 i=0; i<pools.size(); i++)
    {
        ResetPools(i);
        vkDestroyDescriptorPool(this->device, pools[i].pool, NULL);
    }
}

MdDescriptorAllocator uniform_allocator;
VkResult mdCreateDescriptorAllocator(MdRenderer &renderer)
{
    return uniform_allocator.Init(renderer.context->device);
}

void mdDescriptorSetWriteImage( MdRenderer &renderer, 
                                VkDescriptorSet dst, 
                                u32 binding_index, 
                                MdGPUTexture &texture, 
                                VkImageLayout layout)
{
    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = layout;
    image_info.imageView = texture.image_view;
    image_info.sampler = texture.sampler;

    VkWriteDescriptorSet write_set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_set.dstSet = dst;
    write_set.dstBinding = binding_index;
    write_set.pImageInfo = &image_info;

    vkUpdateDescriptorSets(renderer.context->device, 1, &write_set, 0, NULL);
}

void mdDescriptorSetWriteUBO(   MdRenderer &renderer, 
                                VkDescriptorSet dst, 
                                u32 binding_index, 
                                usize offset, 
                                usize range, 
                                MdGPUBuffer &buffer)
{
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = buffer.buffer;
    buffer_info.offset = offset;
    buffer_info.range = range;

    VkWriteDescriptorSet write_set = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write_set.dstSet = dst;
    write_set.dstBinding = binding_index;
    write_set.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(renderer.context->device, 1, &write_set, 0, NULL);
}

VkResult mdCreateGraphicsPipeline(  MdRenderer &renderer, 
                                    MdShaderSource &shaders, 
                                    MdPipelineGeometryInputState *p_geometry_state, 
                                    MdPipelineRasterizationState *p_raster_state, 
                                    MdPipelineColorBlendState *p_color_blend_state, 
                                    const std::string &pass,
                                    MdPipeline &pipeline)
{
    // Find renderpass from its name
    VkRenderPass rp = mdRenderGraphGetPass(pass);
    if (rp == VK_NULL_HANDLE)
    {
        LOG_ERROR("failed to build pipeline");
        return VK_ERROR_UNKNOWN;
    }

    //std::vector<VkDescriptorSet> sets;
    usize layout_count = 0;
    pipeline.set_layouts[layout_count++] = renderer_state.global_layout;
    pipeline.set_layouts[layout_count++] = renderer_state.camera_set_layout;
    
    // Setup descriptor set layouts
    VkResult result;
    if (shaders.bindings.size() > 0)
    {
        VkDescriptorSetLayout layout;
        VkDescriptorSetLayoutCreateInfo set_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_layout_info.bindingCount = shaders.bindings.size();
        set_layout_info.pBindings = shaders.bindings.data();
        set_layout_info.flags = 0;
        result = vkCreateDescriptorSetLayout(renderer.context->device, &set_layout_info, NULL, &layout);
        
        if (result != VK_SUCCESS) return result;

        pipeline.set_layouts[layout_count++] = layout;
    }
    
    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = layout_count;
    layout_info.pSetLayouts = pipeline.set_layouts.data();
    layout_info.flags = 0;
    layout_info.pushConstantRangeCount = 0;    // TO-DO: actually use push constants
    layout_info.pPushConstantRanges = NULL;

    // Create pipeline layout
    result = vkCreatePipelineLayout(renderer.context->device, &layout_info, NULL, &pipeline.layout);
    VK_CHECK(result, "failed to create pipeline layout");

    // If any of these pipeline state infos are left NULL, use the defaults
    MdPipelineGeometryInputState default_geometry_state;
    MdPipelineRasterizationState default_raster_state;
    MdPipelineColorBlendState default_color_blend_state; 
    
    if (p_geometry_state == NULL)
    {
        mdInitGeometryInputState(default_geometry_state);
        mdBuildGeometryInputState(default_geometry_state);
        p_geometry_state = &default_geometry_state;
    }
    
    if (p_raster_state == NULL)
    {
        mdBuildDefaultRasterizationState(default_raster_state);
        p_raster_state = &default_raster_state;
    }

    if (p_color_blend_state == NULL)
    {
        mdBuildDefaultColorBlendState(default_color_blend_state);
        p_color_blend_state = &default_color_blend_state;
    }

    // Setup dynamic states
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

    // Multisample state, for now do nothing
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

    // Depth stencil state, always enabled
    VkPipelineDepthStencilStateCreateInfo depth_info = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    {
        depth_info.depthWriteEnable = VK_TRUE;
        depth_info.depthTestEnable = VK_TRUE;
        depth_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_info.depthBoundsTestEnable = VK_FALSE;
        depth_info.minDepthBounds = 0.0f;
        depth_info.maxDepthBounds = 1.0f;
        depth_info.stencilTestEnable = VK_FALSE;
        depth_info.front = {};
        depth_info.back = {};
    }

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    {
        pipeline_info.flags = 0;
        pipeline_info.basePipelineIndex = -1;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_info.layout = pipeline.layout;
        pipeline_info.renderPass = rp;
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
    }
    result = vkCreateGraphicsPipelines(renderer.context->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline.pipeline);
    VK_CHECK(result, "failed to create graphics pipeline");
    
    return result;
}

void mdDestroyPipeline(MdRenderer &renderer, MdPipeline &pipeline)
{
    vkDestroyPipeline(renderer.context->device, pipeline.pipeline, NULL);
    vkDestroyPipelineLayout(renderer.context->device, pipeline.layout, NULL);
    vkDestroyDescriptorSetLayout(renderer.context->device, pipeline.set_layouts[2], NULL);
}

VkResult mdCreateMaterial(MdRenderer &renderer, MdPipeline &pipeline, MdMaterial &material)
{
    VkResult result = uniform_allocator.AllocateSets(pipeline.set_layouts[2], 2, 1, &material.set);
    if (result != VK_SUCCESS) return result;

    material.pipeline = pipeline.pipeline;
    return result;
}

void mdMaterialSetImage(MdRenderer &renderer, MdMaterial& material, u32 binding_index, MdGPUTexture &texture, VkImageLayout layout)
{
    mdDescriptorSetWriteImage(renderer, material.set, binding_index, texture, layout);
}

void mdMaterialSetUBO(MdRenderer &renderer, MdMaterial& material, u32 binding_index, usize offset, usize range, MdGPUBuffer &buffer)
{
    mdDescriptorSetWriteUBO(renderer, material.set, binding_index, offset, range, buffer);
}
#pragma endregion

/*
#pragma region [ Mesh List ]
struct MdMeshList
{
    std::map<u32, std::vector<VkBuffer>> meshes;
    std::map<u32, std::vector<VkDeviceSize>> offsets;

    MdMeshList(){}

    void AddMesh(VkBuffer mesh, VkDeviceSize size, u32 material_index = UINT32_MAX);
    void RemoveMesh(VkBuffer mesh);
    bool UpdateMesh(VkBuffer mesh, u32 material_index);
};

void MdMeshList::AddMesh(VkBuffer mesh, VkDeviceSize size, u32 material_index)
{
    auto res = meshes.find(material_index);
    if (res == meshes.end())
    {
        meshes.emplace(std::pair<u32, VkBuffer>(material_index, mesh));
        offsets.emplace(std::pair<u32, VkDeviceSize>(material_index, size));
    }
    else
    {
        meshes[material_index].push_back(mesh);
        offsets[material_index].push_back(size);
    }
}

void MdMeshList::RemoveMesh(VkBuffer mesh)
{
    u32 idx = UINT32_MAX;
    u32 material_idx = UINT32_MAX;

    for (auto m_it = meshes.begin(); m_it != meshes.end(); m_it++)
    {
        auto mesh_list = &meshes.at(m_it->first);
        for (auto it = mesh_list->begin(); it != mesh_list->end(); it++)
        {
            if (*it != mesh)
                continue;
            
            idx = std::distance(mesh_list->begin(), it);
            material_idx = m_it->first;

            mesh_list->erase(it);
        }
    }
    
    if (idx == UINT32_MAX) return;
    offsets[material_idx].erase(offsets[material_idx].begin() + idx);
}

bool MdMeshList::UpdateMesh(VkBuffer mesh, u32 material_index)
{
    u32 idx = UINT32_MAX;
    u32 material_idx = UINT32_MAX;
    
    VkDeviceSize size = 0;

    // Remove the mesh from the list if it doesn't exist already
    for (auto m_it = meshes.begin(); m_it != meshes.end(); m_it++)
    {
        auto mesh_list = &meshes.at(m_it->first);
        for (auto it = mesh_list->begin(); it != mesh_list->end(); it++)
        {
            if (*it != mesh)
                continue;
            
            idx = std::distance(mesh_list->begin(), it);
            material_idx = m_it->first;

            size = offsets[material_idx][idx];
            mesh_list->erase(it);
        }
    }
    
    // If the mesh hasn't been found already, break early
    if (idx == UINT32_MAX) return false;
    offsets[material_idx].erase(offsets[material_idx].begin() + idx);
    
    AddMesh(mesh, size, material_index);
    return true;
}
#pragma endregion
*/

MdRenderContext renderer_context;
VkExtent2D shadow_map_extent = {8192, 8192};

// [TEMP FUNCTION] allocates global sets, layouts, and UBO
VkResult mdCreateGlobalSetsAndLayouts(MdRenderer &renderer)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    bindings.push_back({
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_ALL,
        .pImmutableSamplers = NULL
    });
    
    VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();
    layout_info.flags = 0;

    VkResult result = vkCreateDescriptorSetLayout(
        renderer.context->device, 
        &layout_info, 
        NULL, 
        &renderer_state.global_layout
    );
    if (result != VK_SUCCESS) return result;

    result = uniform_allocator.AllocateSets(
        renderer_state.global_layout, 
        0, 
        1, 
        &renderer_state.global_set
    );
    if (result != VK_SUCCESS) return result;

    return result;
}

// [TEMP FUNCTION] allocates global sets, layouts, and UBO
VkResult mdCreateMainCameraSetsAndLayouts(MdRenderer &renderer)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    bindings.push_back({
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_ALL,
        .pImmutableSamplers = NULL
    });

    VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();
    layout_info.flags = 0;

    VkResult result = vkCreateDescriptorSetLayout(
        renderer.context->device, 
        &layout_info, 
        NULL, 
        &renderer_state.camera_set_layout
    );
    if (result != VK_SUCCESS) return result;

    VkDescriptorSet cam_set = VK_NULL_HANDLE;
    result = uniform_allocator.AllocateSets(
        renderer_state.camera_set_layout, 
        1, 
        1, 
        &cam_set
    );
    if (result != VK_SUCCESS) return result;

    renderer_state.camera_sets.push_back(cam_set);

    return result;
}


struct MdNode
{
    char name[128];
    Matrix4x4 transform;
    u32 parent;
    std::vector<u32> children;
};

struct MdNodeList
{
    std::vector<MdNode> nodes;
    u32 size;
};

struct MdScene
{
    char scene_name[128];
    MdNodeList node_list;
};

struct MdDrawList
{

};

#pragma region [ Camera ]

struct MdCamera
{
    Matrix4x4 view_projection;
};

#pragma endregion

MdResult mdCreateRendererState(MdRenderer &renderer);
void mdDestroyRendererState(MdRenderer &renderer);

MdResult mdCreateRendererState(MdRenderer &renderer)
{
    // Create graphics queue
    MdResult result = mdGetQueue(VK_QUEUE_GRAPHICS_BIT, *renderer.context, renderer_state.graphics_queue);
    VkResult vk_result = VK_ERROR_UNKNOWN;
    
    if (result != MD_SUCCESS) goto fail;

    // Create GPU memory allocator
    vk_result = mdCreateGPUAllocator(
        *renderer.context, 
        renderer_state.allocator, 
        renderer_state.graphics_queue
    );
    if (vk_result != VK_SUCCESS) { result = MD_ERROR_UNKNOWN; goto fail; }

    // Create uniform allocator
    vk_result = mdCreateDescriptorAllocator(renderer);
    if (vk_result != VK_SUCCESS) { result = MD_ERROR_UNKNOWN; goto fail; }

    mdCreateGlobalSetsAndLayouts(renderer);
    mdCreateMainCameraSetsAndLayouts(renderer);

    mdRenderGraphInit(renderer.context);
    return result;

    fail:
        mdDestroyRendererState(renderer);
        return result;
}

void mdDestroyRendererState(MdRenderer &renderer)
{
    mdRenderGraphDestroy();
    mdDestroyGPUAllocator(renderer_state.allocator);
}

MdResult mdCreateRenderer(u16 w, u16 h, const char *p_name, MdRenderer &renderer)
{
    std::vector<const char*> instance_extensions;
    u16 count = 0;
    
    VkResult vk_result = VK_ERROR_UNKNOWN;
    MdResult result = mdInitWindowSubsystem();
    if (result != MD_SUCCESS) goto fail;

    result = mdCreateWindow(w, h, p_name, renderer.window);
    if (result != MD_SUCCESS) goto fail;

    // Init render context
    mdWindowQueryRequiredVulkanExtensions(renderer.window, NULL, &count);
    instance_extensions.reserve(count);
    mdWindowQueryRequiredVulkanExtensions(renderer.window, instance_extensions.data(), &count);
    
    renderer.context = &renderer_context;
    result = mdInitContext(*renderer.context, instance_extensions);
    if (result != MD_SUCCESS) goto fail;
    
    mdWindowGetSurfaceKHR(renderer.window, renderer.context->instance, &renderer.context->surface);
    if (renderer.context->surface == VK_NULL_HANDLE) goto fail;
    
    result = mdCreateDevice(*renderer.context);
    if (result != MD_SUCCESS) goto fail;

    result = mdGetSwapchain(*renderer.context);
    if (result != MD_SUCCESS) goto fail;

    // Renderer state initialization
    result = mdCreateRendererState(renderer);
    if (result != MD_SUCCESS) goto fail;

    return MD_SUCCESS;
    
    fail:
        mdDestroyRenderer(renderer);
        return result;
}

void mdDestroyRenderer(MdRenderer &renderer)
{
    mdDestroyRendererState(renderer);

    mdDestroyContext(*renderer.context);
    mdDestroyWindow(renderer.window);
    mdDestroyWindowSubsystem();
}

VkResult buildDefualtShaders(   MdRenderer &renderer, 
                                MdPipelineGeometryInputState &geometry_state,
                                MdPipelineRasterizationState &raster_state,
                                MdPipelineColorBlendState &color_state,
                                MdPipeline &pipeline)
{
    MdShaderSource shaders;
    mdLoadShaderSPIRV(*renderer.context, 0, NULL, VK_SHADER_STAGE_VERTEX_BIT, shaders);
    mdLoadShaderSPIRV(*renderer.context, 0, NULL, VK_SHADER_STAGE_FRAGMENT_BIT, shaders);
    mdShaderAddBinding(shaders, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL);
    
    VkResult result = mdCreateGraphicsPipeline(
        renderer, 
        shaders, 
        &geometry_state, 
        &raster_state, 
        &color_state, 
        "final", 
        pipeline
    );
    mdDestroyShaderSource(*renderer.context, shaders);

    return VK_ERROR_UNKNOWN;//result;
}

void test()
{
    MdRenderer renderer;
    mdCreateRenderer(1920, 1080, "Midori Engine", renderer);
 
    MdScene scene;
    //mdCreateScene(scene);

    MdCamera camera;
    //mdAddCamera();

    MdPipelineGeometryInputState geometry_state;
    MdPipelineRasterizationState raster_state;
    MdPipelineColorBlendState color_state;

    mdBuildDefaultGeometryInputState(geometry_state);
    mdBuildDefaultRasterizationState(raster_state);
    mdBuildDefaultColorBlendState(color_state);

    MdPipeline pipeline;
    buildDefualtShaders(
        renderer, 
        geometry_state, 
        raster_state, 
        color_state, 
        pipeline
    );

    MdMaterial material;
    mdCreateMaterial(renderer, pipeline, material);
    
    //mdAddModel();

    //do
    //{
    //    //mdRendererDraw(renderer);
    //}
    //while(!mdShouldQuit());

    mdDestroyRenderer(renderer);
}