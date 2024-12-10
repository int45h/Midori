#pragma once
#include <typedefs.h>
#if defined(MD_USE_SDL)
#include <SDL2/SDL.h>
enum MdMouseButtonFlags
{
    MD_LEFT_BUTTON = 1,
    MD_MIDDLE_BUTTON = 2,
    MD_RIGHT_BUTTON = 4
};
typedef u16 MdMouseButton;
#if defined(MD_USE_VULKAN)
#include <vulkan/vulkan_core.h>
#include <SDL2/SDL_vulkan.h>
#endif

#elif defined(__unix__)
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#if defined(MD_USE_VULKAN)
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_xcb.h>
#endif
enum MdMouseButtonFlags
{
    MD_LEFT_BUTTON = XCB_BUTTON_MASK_1,
    MD_MIDDLE_BUTTON = XCB_BUTTON_MASK_2,
    MD_RIGHT_BUTTON = XCB_BUTTON_MASK_3
};
typedef u16 MdMouseButton;

#endif