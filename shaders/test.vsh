#version 450

layout (location=0) in vec3 vpos;
layout (location=1) in vec3 vnorm;
layout (location=2) in vec2 vuv;

layout (set=0, binding=0) uniform UBO
{
    mat4 u_model;
    mat4 u_view;
    mat4 u_projection;
    vec2 u_resolution;
    float u_time;
}
ubo;

layout (location=0) out vec3 norm;
layout (location=1) out vec2 uv;
layout (location=2) out vec3 pos;
layout (location=3) out vec3 frag_pos;

void main()
{
    gl_Position = ubo.u_projection*ubo.u_view*ubo.u_model*vec4(vpos, 1.0);
    norm = vnorm;
    uv = vuv;
    pos = vpos;
    frag_pos = (ubo.u_model * vec4(vpos,1.)).xyz;
}