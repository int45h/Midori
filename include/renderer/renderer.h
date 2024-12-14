#pragma once
#include <renderer_api.h>
#include <platform/window/window.h>

struct MdRenderContext;

struct MdCamera;
struct MdPipeline;
struct MdMaterial;

struct MdRenderer
{
    MdWindow window;
    MdRenderContext *context;
};

MdResult mdCreateRenderer(u16 w, u16 h, const char *p_title, MdRenderer &renderer);
void mdDestroyRenderer(MdRenderer &renderer);
