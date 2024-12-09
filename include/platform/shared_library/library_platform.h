#pragma once

#if defined(__unix__)
#include <dlfcn.h>
typedef void* MdLibraryHandle;
#elif defined(__WIN32__)
typedef FARPROC MdLibraryHandle;
#endif