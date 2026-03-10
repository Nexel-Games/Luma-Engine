#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform PerDrawData
{
    mat4 viewProjection;
    mat4 worldTransform;
    vec4 tint;
} uPerDraw;

layout(location = 0) out vec3 vColor;

void main()
{
    vec4 clip = uPerDraw.viewProjection * uPerDraw.worldTransform * vec4(inPosition, 1.0);
    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);
    vColor = inColor * uPerDraw.tint.rgb;
}
