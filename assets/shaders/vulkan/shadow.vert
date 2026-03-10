#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 2) uniform sampler2D uHeightTex;

layout(set = 0, binding = 0) uniform ShadowData
{
    mat4 uShadowViewProjection;
    mat4 uWorldTransform;
    vec4 uUvTransform0;
    vec4 uShadowMaterialParams;
};

layout(location = 0) out vec2 vUV;

#ifndef USES_WORLD_POSITION_OFFSET
#define USES_WORLD_POSITION_OFFSET 0
#endif

void main()
{
    float uvRotation = uShadowMaterialParams.x;
    float s = sin(uvRotation);
    float c = cos(uvRotation);
    vec2 centeredUv = (aUV - vec2(0.5)) * uUvTransform0.xy;
    vec2 rotatedUv = vec2(
        centeredUv.x * c - centeredUv.y * s,
        centeredUv.x * s + centeredUv.y * c);
    vUV = rotatedUv + vec2(0.5) + uUvTransform0.zw;

    vec3 localPosition = aPosition;
#if USES_WORLD_POSITION_OFFSET
    float displacement = uShadowMaterialParams.w;
    if (abs(displacement) > 1.0e-6)
    {
        float height = textureLod(uHeightTex, vUV, 0.0).r * 2.0 - 1.0;
        localPosition += vec3(0.0, 1.0, 0.0) * (height * displacement);
    }
#endif

    gl_Position = uShadowViewProjection * uWorldTransform * vec4(localPosition, 1.0);
}
