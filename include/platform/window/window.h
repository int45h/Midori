#pragma once

#include "window_platform.h"

typedef void(*MdMouseMotionCallback)(u16 x, u16 y);
typedef void(*MdMousePressedCallback)(u16 x, u16 y, MdMouseButton button);
typedef void(*MdMouseReleasedCallback)(u16 x, u16 y, MdMouseButton button);
typedef void(*MdKeyboardPressedCallback)(i32 keycode);
typedef void(*MdKeyboardReleasedCallback)(i32 keycode);
typedef void(*MdWindowResizeCallback)(u16 w, u16 h);

struct MdWindowContextData
{
    MdMouseMotionCallback mouse_callback = NULL;
    MdMousePressedCallback mouse_pressed_callback = NULL;
    MdMouseReleasedCallback mouse_released_callback = NULL;
    MdKeyboardPressedCallback keyboard_pressed_callback = NULL;
    MdKeyboardReleasedCallback keyboard_released_callback = NULL;
    MdWindowResizeCallback window_resize_callback = NULL;

    bool closing = false;
};

struct MdWindowContext;
enum MdWindowPosition
{
    MD_WINDOWPOS_UNDEFINED,
    MD_WINDOWPOS_CENTER
};

struct MdWindow
{
    u16 w, h;
    char name[128];

    MdWindowContext *context;
};

void mdWindowRegisterMousePressedCallback(MdWindow &window, MdMousePressedCallback callback);
void mdWindowRegisterMouseReleasedCallback(MdWindow &window, MdMouseReleasedCallback callback);
void mdWindowRegisterMouseMotionCallback(MdWindow &window, MdMouseMotionCallback callback);
void mdWindowRegisterKeyboardPressedCallback(MdWindow &window, MdKeyboardPressedCallback callback);
void mdWindowRegisterKeyboardReleasedCallback(MdWindow &window, MdKeyboardReleasedCallback callback);
void mdWindowRegisterWindowResizedCallback(MdWindow &window, MdWindowResizeCallback callback);

MdResult mdInitWindowSubsystem();
void mdDestroyWindowSubsystem();
MdResult mdCreateWindow(u16 w, u16 h, const char *title, MdWindow &window);
void mdDestroyWindow(MdWindow &window);
bool mdWindowShouldClose(MdWindow &window);
void mdPollEvent(MdWindow &window);
u32 mdGetTicks();

// Vulkan specific extensions
#if defined(MD_USE_VULKAN)
void mdWindowQueryRequiredVulkanExtensions(MdWindow &window, const char **p_extensions, u16 *size);
VkResult mdWindowGetSurfaceKHR(MdWindow &window, VkInstance instance, VkSurfaceKHR *p_surface);
#endif

#define MD_DEFINE_CALLBACKS \
void mdWindowRegisterMousePressedCallback(MdWindow &window, MdMousePressedCallback callback){ window.context->metadata.mouse_pressed_callback = callback; }\
void mdWindowRegisterMouseReleasedCallback(MdWindow &window, MdMouseReleasedCallback callback){ window.context->metadata.mouse_released_callback = callback; }\
void mdWindowRegisterMouseMotionCallback(MdWindow &window, MdMouseMotionCallback callback){ window.context->metadata.mouse_callback = callback; }\
void mdWindowRegisterKeyboardPressedCallback(MdWindow &window, MdKeyboardPressedCallback callback){ window.context->metadata.keyboard_pressed_callback = callback; }\
void mdWindowRegisterKeyboardReleasedCallback(MdWindow &window, MdKeyboardReleasedCallback callback){ window.context->metadata.keyboard_released_callback = callback; }\
void mdWindowRegisterWindowResizedCallback(MdWindow &window, MdWindowResizeCallback callback){ window.context->metadata.window_resize_callback = callback; }\
