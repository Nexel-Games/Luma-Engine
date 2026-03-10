#version 450

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 1) uniform sampler2D uOpacityTex;

layout(set = 0, binding = 0) uniform ShadowData
{
    mat4 uShadowViewProjection;
    mat4 uWorldTransform;
    vec4 uUvTransform0;
    vec4 uShadowMaterialParams;
};

layout(location = 0) out vec4 outColor;

#ifndef MATERIALBLENDING_MASKED
#define MATERIALBLENDING_MASKED 0
#endif

#ifndef USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL
#define USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL 0
#endif

float InterleavedGradientNoise(vec2 pixelPos)
{
    return fract(52.9829189 * fract(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}

void main()
{
    float finalOpacity = clamp(uShadowMaterialParams.y * texture(uOpacityTex, vUV).r, 0.0, 1.0);
#if MATERIALBLENDING_MASKED
    if (finalOpacity < uShadowMaterialParams.z)
    {
        discard;
    }
#endif
#if USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL
    if (finalOpacity < 0.999 && InterleavedGradientNoise(gl_FragCoord.xy) > finalOpacity)
    {
        discard;
    }
#endif
    float depth = gl_FragCoord.z;
    outColor = vec4(depth, depth, depth, 1.0);
}
