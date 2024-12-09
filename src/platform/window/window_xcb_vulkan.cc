#pragma once

#define MD_USE_VULKAN
#include "../../../include/typedefs.h"
#include "../../../include/platform/window/window.h"

#include <stdlib.h>
#include <string.h>

struct MdWindowContext
{
    xcb_connection_t *p_conn;
    const xcb_setup_t *p_x_server_info;
    xcb_screen_iterator_t iterator;
    xcb_screen_t *p_screen;
    xcb_intern_atom_reply_t *p_closing_reply;

    xcb_window_t id;
    int display_index;

    MdWindowContextData metadata;
};
MdWindowContext context = {};

void mdWindowRegisterMousePressedCallback(MdWindow &window, MdMousePressedCallback callback){ window.context->metadata.mouse_pressed_callback = callback; }
void mdWindowRegisterMouseReleasedCallback(MdWindow &window, MdMouseReleasedCallback callback){ window.context->metadata.mouse_released_callback = callback; }
void mdWindowRegisterMouseMotionCallback(MdWindow &window, MdMouseMotionCallback callback){ window.context->metadata.mouse_callback = callback; }
void mdWindowRegisterKeyboardPressedCallback(MdWindow &window, MdKeyboardPressedCallback callback){ window.context->metadata.keyboard_pressed_callback = callback; }
void mdWindowRegisterKeyboardReleasedCallback(MdWindow &window, MdKeyboardReleasedCallback callback){ window.context->metadata.keyboard_released_callback = callback; }
void mdWindowRegisterWindowResizedCallback(MdWindow &window, MdWindowResizeCallback callback){ window.context->metadata.window_resize_callback = callback; }

MdResult mdInitWindowSubsystem()
{
    // Connect to the X server
    context.p_conn = xcb_connect(NULL, &context.display_index);
    if (context.p_conn == NULL)
        return MD_ERROR_XCB_CONNECTION_FAILED;

    // Get server information
    context.p_x_server_info = xcb_get_setup(context.p_conn);
    context.iterator = xcb_setup_roots_iterator(context.p_x_server_info);

    for (int i=0; i<context.display_index; i++)
        xcb_screen_next(&context.iterator);

    // Get screen
    context.p_screen = context.iterator.data;

    return MD_SUCCESS;
}

void mdDestroyWindowSubsystem()
{
    xcb_disconnect(context.p_conn);
}

MdResult mdCreateWindow(u16 w, u16 h, const char *title, MdWindow &window)
{
    usize size = MIN_VAL(strlen(title), 127);
    memcpy(window.name, title, size);
    window.w = w;
    window.h = h;

    // Window ID
    context.id = xcb_generate_id(context.p_conn);
    
    // Setup events
    u32 event_mask = XCB_CW_EVENT_MASK;
    u32 event_values[1] = {
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_BUTTON_PRESS | 
        XCB_EVENT_MASK_BUTTON_RELEASE |  
        XCB_EVENT_MASK_BUTTON_MOTION | 
        XCB_EVENT_MASK_BUTTON_1_MOTION | 
        XCB_EVENT_MASK_BUTTON_2_MOTION | 
        XCB_EVENT_MASK_BUTTON_3_MOTION | 
        XCB_EVENT_MASK_BUTTON_4_MOTION | 
        XCB_EVENT_MASK_BUTTON_5_MOTION | 
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_KEY_RELEASE
    };
    
    // Create the window
    xcb_generic_error_t *err = xcb_request_check(context.p_conn,
    xcb_create_window(
        context.p_conn, 
        XCB_COPY_FROM_PARENT, 
        context.id,
        context.p_screen->root,
        context.p_screen->width_in_pixels >> 1,
        context.p_screen->height_in_pixels >> 1,
        w, 
        h,
        1,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        context.p_screen->root_visual,
        event_mask,
        event_values
    ));
    if (err != NULL) return MD_ERROR_WINDOW_FAILURE;

    // Place the window on screen
    err = xcb_request_check(context.p_conn, 
        xcb_map_window(context.p_conn, context.id)
    );
    if (err != NULL) return MD_ERROR_WINDOW_FAILURE;
    
    // Set window title
    xcb_request_check(context.p_conn, 
    xcb_change_property(
        context.p_conn,
        XCB_PROP_MODE_REPLACE,
        context.id,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        size,
        window.name
    ));
    if (err != NULL) return MD_ERROR_WINDOW_FAILURE;

    // Get closing atom
    xcb_intern_atom_cookie_t closing = xcb_intern_atom_unchecked(context.p_conn, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply(context.p_conn, closing, &err);
    if (err != NULL) return MD_ERROR_WINDOW_FAILURE;

    // Flush events, set window context 
    xcb_flush(context.p_conn);
    window.context = &context;

    return MD_SUCCESS;
}

void mdDestroyWindow(MdWindow &window)
{
    xcb_destroy_window(window.context->p_conn, window.context->id);
}

// Vulkan specific extensions
void mdWindowQueryRequiredVulkanExtensions(char **p_extensions, u16 *size)
{
    if (p_extensions == NULL)
    {
        *size = 2;
        return;
    }

    const char *required_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME
    };

    usize size_1 = strlen(required_extensions[0]);
    usize size_2 = strlen(required_extensions[1]);

    u32 _size = 0;
    VkExtensionProperties *properties = NULL;
    
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &_size, NULL);
    VK_CHECK_VOID(result, "failed to enumerate instance extension");
    properties = (VkExtensionProperties*)malloc(_size*sizeof(VkExtensionProperties));
    vkEnumerateInstanceExtensionProperties(NULL, &_size, properties);

    u32 idx = 0;
    for (u32 i=0; i<_size; i++)
    {
        if (idx > 1) break;
        
        for (u32 j=idx; j<2; j++)
        {
            if (strncmp(properties->extensionName, required_extensions[j], 256) == 0)
                idx++;
        } 
    }

    if (idx < 2)
    {
        LOG_ERROR("One or more extensions missing");
        return;
    }

    p_extensions[0] = (char*)malloc(size_1 * sizeof(char));
    p_extensions[1] = (char*)malloc(size_2 * sizeof(char));

    memcpy(p_extensions[0], required_extensions[0], size_1);
    memcpy(p_extensions[1], required_extensions[1], size_2);
    
    free(properties);

    return;
}

VkResult mdWindowGetSurfaceKHR(MdWindow &window, VkInstance instance, VkSurfaceKHR *p_surface)
{
    VkXcbSurfaceCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
    create_info.connection = window.context->p_conn;
    create_info.window = window.context->id;
    create_info.flags = 0;
    return vkCreateXcbSurfaceKHR(instance, &create_info, NULL, p_surface);
}

void mdHandleEvent();
bool mdWindowShouldClose(MdWindow &window)
{
    mdHandleEvent();
    return window.context->metadata.closing;
}

void mdHandleEvent()
{
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(context.p_conn)) != NULL)
    {
        switch (event->response_type & 0x7F)
        {
            case XCB_CLIENT_MESSAGE:
            {
                xcb_client_message_event_t *ev = (xcb_client_message_event_t*)event;
                if (ev->data.data32[0] == context.p_closing_reply->atom)
                    context.metadata.closing = true;
            }
            xcb_flush(context.p_conn);
            break;
            case XCB_EXPOSE: 
            xcb_flush(context.p_conn);
            break;
            case XCB_KEY_PRESS: 
            {
                if (context.metadata.keyboard_pressed_callback != NULL)
                {
                    xcb_key_press_event_t *kev = (xcb_key_press_event_t*)event;
                    context.metadata.keyboard_pressed_callback(kev->detail);
                }
            }
            xcb_flush(context.p_conn);
            break;
            case XCB_KEY_RELEASE: 
            {
                if (context.metadata.keyboard_pressed_callback != NULL)
                {
                    xcb_key_release_event_t *kev = (xcb_key_press_event_t*)event;
                    context.metadata.keyboard_pressed_callback(kev->detail);
                }
            }
            xcb_flush(context.p_conn);
            break;
            case XCB_MOTION_NOTIFY: 
            {
                if (context.metadata.mouse_callback != NULL)
                {
                    xcb_motion_notify_event_t *mev = (xcb_motion_notify_event_t*)event;
                    context.metadata.mouse_callback(mev->event_x, mev->event_y);
                }
            }
            xcb_flush(context.p_conn);
            break;
            case XCB_BUTTON_PRESS:
            {
                if (context.metadata.mouse_pressed_callback != NULL)
                {
                    xcb_button_press_event_t *mev = (xcb_button_press_event_t*)event;
                    context.metadata.mouse_pressed_callback(mev->event_x, mev->event_y, mev->state);
                }
            }
            xcb_flush(context.p_conn);
            break;
            case XCB_BUTTON_RELEASE:
            {
                if (context.metadata.mouse_released_callback != NULL)
                {
                    xcb_button_release_event_t *mev = (xcb_button_release_event_t*)event;
                    context.metadata.mouse_released_callback(mev->event_x, mev->event_y, mev->state);
                }
            }
            xcb_flush(context.p_conn);
            break;
        }
    }
    free(event);
}