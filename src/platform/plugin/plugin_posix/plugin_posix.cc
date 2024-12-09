#include "../../../../include/typedefs.h"

#include <dlfcn.h>
typedef void* MdScriptHandle;

MdResult mdOpenPlugin(const char *p_filepath, MdScriptHandle *p_handle)
{
    MdScriptHandle handle = dlmopen(LM_ID_BASE, p_filepath, RTLD_NOW);
    if (handle == NULL)
    {
        LOG_ERROR("Failed to load object: %s\n",  dlerror());
        return MD_ERROR_PLUGIN_LOAD_FAILURE;
    }

    *p_handle = handle;
    return MD_SUCCESS;
}

MdResult mdClosePlugin(MdScriptHandle handle)
{
    if (dlclose(handle) != 0)
    {
        LOG_ERROR("Failed to close plugin: %s\n", dlerror());
        return MD_ERROR_PLUGIN_CLOSE_FAILURE;
    }

    return MD_SUCCESS;
}

MdResult mdPluginBindSymbol(MdScriptHandle handle, const char *p_symbol, void **pp_bind_point)
{
    void *data = dlsym(handle, p_symbol);
    if (data == NULL)
    {
        LOG_ERROR("Failed to bind symbol '%s': %s\n", p_symbol, dlerror());
        return MD_ERROR_PLUGIN_BIND_FAILURE;
    }

    *pp_bind_point = data;
    return MD_SUCCESS;
}