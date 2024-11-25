#version 450

layout (location=0) in vec3 vpos;
layout (location=1) in vec3 vnorm;
layout (location=2) in vec2 vuv;

layout (set=0, binding=0) uniform UBO
{
    mat4 u_model;
    mat4 u_view_projection;
    mat4 u_light_view_projection;
    vec2 u_resolution;
    float u_time;
}
ubo;

layout (location=0) out vec3 norm;
layout (location=1) out vec2 uv;
layout (location=2) out vec3 pos;
layout (location=3) out vec4 frag_pos;
layout (location=4) out vec4 frag_pos_ls;

void main()
{
    gl_Position = ubo.u_view_projection*ubo.u_model*vec4(vpos, 1.0);
    norm = vnorm;
    uv = vuv;
    pos = vpos;
    frag_pos = ubo.u_model * vec4(vpos,1.);
    frag_pos_ls = ubo.u_light_view_projection * frag_pos;
}