#version 430 core

in vec3 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(max(vColor, vec3(0.0)), 1.0);
}
