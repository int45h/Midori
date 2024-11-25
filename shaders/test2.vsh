#version 450

vec3 positions[3] = vec3[3](
    vec3(-2,  2, 0),
    vec3( 2,  2, 0),
    vec3( 2, -2, 0)
);

vec2 uvs[3] = vec2[3](
    vec2(-1, 0),
    vec2(+1, 0),
    vec2(+1, 2)
);

layout (set=0, binding=0) uniform UBO
{
    mat4 u_model;
    mat4 u_view_projection;
    mat4 u_light_view_projection;
    vec2 u_resolution;
    float u_time;
}
ubo;

layout (location = 0) out vec2 uv;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex].xy - vec2(1, 1), 0., 1.);
    uv = (uvs[gl_VertexIndex]);
}