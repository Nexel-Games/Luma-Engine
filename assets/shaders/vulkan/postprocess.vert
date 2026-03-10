#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 vUV;

void main()
{
    gl_Position = vec4(inPosition.x, -inPosition.y, inPosition.z, 1.0);
    vUV = vec2(inUV.x, 1.0 - inUV.y);
}
