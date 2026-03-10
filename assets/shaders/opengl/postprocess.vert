#version 430 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

out vec2 vUV;

void main()
{
    gl_Position = vec4(inPosition, 1.0);
    vUV = inUV;
}
