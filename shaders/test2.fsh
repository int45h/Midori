#version 450

layout (location=0) in vec2 uv;

layout (set=0, binding=0) uniform UBO
{
    mat4 u_model;
    mat4 u_view_projection;
    mat4 u_light_view_projection;
    vec2 u_resolution;
    float u_time;
}
ubo;

layout (set=0, binding=2) uniform sampler2D tex;
layout (set=0, binding=3) uniform sampler2D shadow_tex;

layout (location=0) out vec4 fragColor;

// https://www.shadertoy.com/view/lsKSWR
float vignette(vec2 uv, float intensity, float extent)
{
    uv *= 1.-uv.yx;
    
    return pow(uv.x*uv.y*intensity, extent);
}

void main()
{
    vec4 c = texture(tex, uv);
    fragColor = c * vignette(uv, 30., .55);
    //fragColor = texture(shadow_tex, uv);
}