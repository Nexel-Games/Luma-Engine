#version 430 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(std140, binding = 0) uniform PerDrawData
{
    mat4 viewProjection;
    mat4 worldTransform;
    vec4 tint;
} uPerDraw;

out vec3 vColor;

void main()
{
    gl_Position = uPerDraw.viewProjection * uPerDraw.worldTransform * vec4(inPosition, 1.0);
    vColor = inColor * uPerDraw.tint.rgb;
}
