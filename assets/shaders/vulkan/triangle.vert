#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 11) uniform sampler2D uHeightTex;

layout(set = 0, binding = 0) uniform PerDrawData
{
    mat4 viewProjection;
    mat4 worldTransform;
    vec4 tint;
    vec4 emissiveColorIntensity;
    vec4 surfaceParameters;
    vec4 opacityAndNormal;
    vec4 uvTransform0;
    vec4 uvTransform1;
    vec4 materialParameters2;
    vec4 subsurfaceAndCoat;
    vec4 materialParameters3;
    vec4 lightmapParams;
} uPerDraw;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;

#ifndef MATERIAL_TWOSIDED
#define MATERIAL_TWOSIDED 0
#endif

#ifndef USES_WORLD_POSITION_OFFSET
#define USES_WORLD_POSITION_OFFSET 0
#endif

#ifndef USE_VERTEX_COLOR
#define USE_VERTEX_COLOR 0
#endif

void main()
{
    float uvRotation = uPerDraw.uvTransform1.x;
    float s = sin(uvRotation);
    float c = cos(uvRotation);
    vec2 centeredUv = (inUV - vec2(0.5)) * uPerDraw.uvTransform0.xy;
    vec2 rotatedUv = vec2(
        centeredUv.x * c - centeredUv.y * s,
        centeredUv.x * s + centeredUv.y * c);
    vUV = rotatedUv + vec2(0.5) + uPerDraw.uvTransform0.zw;

    vec3 localPosition = inPosition;
#if USES_WORLD_POSITION_OFFSET
    float displacement = uPerDraw.materialParameters2.y;
    if (abs(displacement) > 1.0e-6)
    {
        float height = textureLod(uHeightTex, vUV, 0.0).r * 2.0 - 1.0;
        localPosition += vec3(0.0, 1.0, 0.0) * (height * displacement);
    }
#endif

    vec4 worldPosition = uPerDraw.worldTransform * vec4(localPosition, 1.0);
    vec4 clip = uPerDraw.viewProjection * worldPosition;
    gl_Position = vec4(clip.x, -clip.y, clip.z, clip.w);
#if USE_VERTEX_COLOR
    vColor = inColor;
#else
    vColor = vec3(1.0);
#endif
    vWorldPos = worldPosition.xyz;
}
