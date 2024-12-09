#pragma once

#include <typedefs.h>
#include "library_platform.h"

MdResult mdLoadLibrary(const char *p_filepath, MdLibraryHandle *p_handle);
MdResult mdCloseLibrary(MdLibraryHandle handle);
MdResult mdLibraryBindSymbol(MdLibraryHandle handle, const char *p_symbol, void **pp_bind_point);