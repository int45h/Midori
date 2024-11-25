#version 450

layout (location=0) in vec3 vpos;

layout (set=0, binding=0) uniform UBO
{
    mat4 u_model;
    mat4 u_view_projection;
    mat4 u_light_view_projection;
    vec2 u_resolution;
    float u_time;
}
ubo;

void main()
{
    gl_Position = ubo.u_light_view_projection*ubo.u_model*vec4(vpos, 1.0);
}