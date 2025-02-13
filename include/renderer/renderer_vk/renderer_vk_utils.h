#pragma once
#include <typedefs.h>
#include <vulkan/vulkan_core.h>

#include <vector>
#define MD_NULL_HANDLE -1
template<typename EntryType>
struct MdHandleList
{
    VkDevice device = VK_NULL_HANDLE;
    std::vector<EntryType> entries;
    u32 handle_index = 0;

    bool Get(u32 handle, u32 *p_index);
    bool Add(EntryType *p_type, u32 *p_handle);
    bool Remove(u32 handle);
    bool Clear(VkDevice device = VK_NULL_HANDLE);
    void SetDevice(VkDevice device);
    u32 GenerateHandle();

    void (*DestroyCallback)(EntryType *p_type, VkDevice device);
};