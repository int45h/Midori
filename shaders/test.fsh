#version 450

layout (location=0) in vec3 vnorm;
layout (location=1) in vec2 vuv;
layout (location=2) in vec3 vpos;
layout (location=3) in vec3 frag_pos;

layout (location=0) out vec4 fragColor;

layout (set=0, binding=0) uniform UBO
{
    mat4 u_model;
    mat4 u_view;
    mat4 u_projection;
    vec2 u_resolution;
    float u_time;
}
ubo;

layout (set=0, binding=1) uniform sampler2D tex;

void main()
{
    float t = ubo.u_time;
    vec3 lp = (ubo.u_model*vec4(5.*cos(t), 3., 5.*sin(t), 1.)).xyz;
    float li = 1000.;

    vec3 L = frag_pos - lp;
    float a = length(L);
    a = li / (a*a);
    L = normalize(L);

    vec3 V = normalize(vec3(0,0,0) - frag_pos);
    vec3 H = normalize(L+V);
    float kS = pow(max(dot(vnorm,H),0.),256.);
    float kD = max(dot(L, vnorm), 0.)*a;
    float kA = .1;
    vec3 lit = vec3(kA+kD+kS);

    fragColor = texture(tex, vuv) * vec4(lit, 1.);
    //fragColor = vec4(frag_pos, 1.);
    
    //fragColor = vec4(.5+.5*vnorm, 1.);

}