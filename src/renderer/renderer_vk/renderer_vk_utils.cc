#include <renderer_vk/renderer_vk_utils.h>

#pragma region [ Handle List Helper Class ]
template<typename EntryType>
bool MdHandleList<EntryType>::Get(u32 handle, u32 *p_index)
{
    if (handle == MD_NULL_HANDLE)
        return false;

    if (entries[handle].id == MD_NULL_HANDLE || entries[handle].id != handle)
        return false;

    *p_index = handle;
    return true;
    
}

template<typename EntryType>
bool MdHandleList<EntryType>::Add(EntryType *p_type, u32 *p_handle)
{
    u32 handle = GenerateHandle();
    if (handle == (u32)MD_NULL_HANDLE) 
        return false;

    if (handle == handle_index)
    {
        entries.push_back({.id = (u32)MD_NULL_HANDLE});
        handle_index++;
    }
    p_type->id = handle;
    
    entries[handle] = *p_type;
    *p_handle = handle;

    return true;
}

template<typename EntryType>
bool MdHandleList<EntryType>::Remove(u32 handle)
{
    u32 index = 0;
    if (!Get(handle, &index))
        return false;

    entries[index].id = MD_NULL_HANDLE;
    if (this->DestroyCallback != NULL)
        this->DestroyCallback(&entries[index], this->device);

    return true;
}

template<typename EntryType>
bool MdHandleList<EntryType>::Clear(VkDevice device)
{
    if (DestroyCallback == NULL) 
        return false;

    for (u32 i=0; i<entries.size(); i++)
        DestroyCallback(&entries[i], device);

    entries.clear();
    return true;
}

template<typename EntryType>
u32 MdHandleList<EntryType>::GenerateHandle()
{
    for (u32 i=0; i<entries.size(); i++)
    {
        if (entries[i].id == MD_NULL_HANDLE)
            return i;
    }
    
    if ((u32)(handle_index + 1) < handle_index)
        return MD_NULL_HANDLE;
    
    return handle_index;
}
#pragma endregion