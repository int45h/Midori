#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#define MD_USE_SDL
#define MD_USE_VULKAN
#include <typedefs.h>
#include <window/window.h>

#include <stdlib.h>
#include <string.h>

struct MdWindowContext
{
    SDL_Window *window;
    MdWindowContextData metadata;

    SDL_Event event;
};
MdWindowContext renderer_context;

MD_DEFINE_CALLBACKS

MdResult mdInitWindowSubsystem()
{
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        return MD_ERROR_WINDOW_FAILURE;

    #if defined(MD_USE_VULKAN)
    SDL_Vulkan_LoadLibrary(NULL);
    #endif
    return MD_SUCCESS;
}

void mdDestroyWindowSubsystem()
{
    SDL_Quit();
}

MdResult mdCreateWindow(u16 w, u16 h, const char *title, MdWindow &window)
{
    usize size = MIN_VAL(strlen(title), 127);
    memcpy(window.name, title, size);
    window.w = w;
    window.h = h;

    renderer_context.window = SDL_CreateWindow(
        title, 
        SDL_WINDOWPOS_CENTERED, 
        SDL_WINDOWPOS_CENTERED, 
        w, 
        h, 
        SDL_WINDOW_VULKAN
    );

    if (renderer_context.window == NULL)
    {
        LOG_ERROR("Failed to create window: %s\n", SDL_GetError());
        return MD_ERROR_WINDOW_FAILURE;
    }

    window.context = &renderer_context;
    window.context->metadata.closing = false;
    return MD_SUCCESS;
}

void mdDestroyWindow(MdWindow &window)
{
    SDL_DestroyWindow(window.context->window);
}

void mdPollEvent(MdWindow &window)
{
    while (SDL_PollEvent(&renderer_context.event))
    {
        switch (renderer_context.event.type)
        {
            case SDL_QUIT:
            renderer_context.metadata.closing = true;
            break;
            case SDL_WINDOWEVENT_RESIZED:
            {
                if (renderer_context.metadata.window_resize_callback != NULL)
                {
                    int _w = 0, _h = 0;
                    SDL_GetWindowSizeInPixels(window.context->window, &_w, &_h);
                    
                    window.w = _w;
                    window.h = _h;
                    renderer_context.metadata.window_resize_callback(_w, _h);
                }
            }
            break;
            case SDL_MOUSEMOTION:
            {
                if (renderer_context.metadata.mouse_callback != NULL)
                {
                    int _x = 0, _y = 0;
                    SDL_GetMouseState(&_x, &_y);
                    renderer_context.metadata.mouse_callback(_x, _y);
                }
            }
            break;
            case SDL_MOUSEBUTTONDOWN:
            {
                if (renderer_context.metadata.mouse_pressed_callback != NULL)
                {
                    int _x = 0, _y = 0;
                    u32 button = SDL_GetMouseState(&_x, &_y);
                    renderer_context.metadata.mouse_pressed_callback(_x, _y, button & 7);
                }
            }
            break;
            case SDL_MOUSEBUTTONUP:
            {
                if (renderer_context.metadata.mouse_released_callback != NULL)
                {
                    int _x = 0, _y = 0;
                    u32 button = SDL_GetMouseState(&_x, &_y);
                    renderer_context.metadata.mouse_released_callback(_x, _y, button & 7);
                }
            }
            break;
            case SDL_KEYDOWN:
            {
                if (renderer_context.metadata.keyboard_pressed_callback != NULL)
                {
                    renderer_context.metadata.keyboard_pressed_callback(renderer_context.event.key.keysym.sym);
                }
            }
            break;
            case SDL_KEYUP:
            {
                if (renderer_context.metadata.keyboard_released_callback != NULL)
                {
                    renderer_context.metadata.keyboard_released_callback(renderer_context.event.key.keysym.sym);
                }
            }
            break;
        }
    }
}

bool mdWindowShouldClose(MdWindow &window)
{
    return window.context->metadata.closing;    
}

// Vulkan specific extensions
void mdWindowQueryRequiredVulkanExtensions(MdWindow &window, const char **p_extensions, u16 *size)
{
    unsigned int _s;
    SDL_Vulkan_GetInstanceExtensions(
        window.context->window, 
        &_s, 
        (p_extensions != NULL) ? (const char**)p_extensions : NULL
    );
    *size = _s;
}

VkResult mdWindowGetSurfaceKHR(MdWindow &window, VkInstance instance, VkSurfaceKHR *p_surface)
{
    SDL_Vulkan_CreateSurface(window.context->window, instance, p_surface);
    return VK_SUCCESS;
}

u32 mdGetTicks() { return SDL_GetTicks(); }