#version 430 core

in vec3 vColor;
in vec2 vUV;
in vec3 vWorldPos;
layout(binding = 1) uniform sampler2D uAlbedoTex;
layout(binding = 2) uniform sampler2D uNormalTex;
layout(binding = 3) uniform sampler2D uORMTex;
layout(binding = 12) uniform sampler2D uMetallicTex;
layout(binding = 13) uniform sampler2D uRoughnessTex;
layout(binding = 14) uniform sampler2D uAmbientOcclusionTex;
layout(binding = 9) uniform sampler2D uEmissiveTex;
layout(binding = 10) uniform sampler2D uOpacityTex;
layout(binding = 11) uniform sampler2D uHeightTex;
layout(binding = 16) uniform sampler2D uLightmapTex;
layout(binding = 5) uniform sampler2D uIrradianceTex;
layout(binding = 6) uniform sampler2D uPrefilteredEnvironmentTex;
layout(binding = 7) uniform sampler2D uDirectionalShadowTex;
layout(binding = 8) uniform sampler2D uSpotShadowTex;
layout(binding = 15) uniform sampler2D uPointShadowTex;
layout(std140, binding = 0) uniform PerDrawData
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
layout(std140, binding = 4) uniform LightingData
{
    vec4 ambientColorIntensity;
    vec4 directionalDirectionIntensity;
    vec4 directionalColorEnabled;
    vec4 cameraWorldPosition;
    vec4 iblDiffuseColorIntensity;
    vec4 iblSpecularColorIntensity;
    vec4 iblParams;
    vec4 iblRotationAndFlags;
    vec4 iblLowerHemisphereColorFlag;
    vec4 iblExposureAndSun;
    vec4 postProcessToneMap;
    vec4 postProcessColor;
    vec4 postProcessCurve;
    vec4 postProcessBloom;
    vec4 postProcessColorBalance;
    vec4 postProcessFilmCurve;
    vec4 localLightCounts;
    vec4 pointPositionRange[4];
    vec4 pointColorIntensity[4];
    vec4 spotPositionRange[4];
    vec4 spotDirectionInner[4];
    vec4 spotColorOuter[4];
    mat4 directionalShadowMatrix;
    mat4 pointShadowMatrices[6];
    mat4 spotShadowMatrix;
    vec4 directionalShadowParams;
    vec4 pointShadowParams;
    vec4 pointShadowLightPositionRange;
    vec4 pointShadowAtlasInvSize;
    vec4 spotShadowParams;
} uLighting;
layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265359;
const float kPrefilterLevels = 5.0;
const int kBlendModeMasked = 1;
const int kBlendModeTranslucent = 2;
const int kBlendModeAdditive = 3;
const int kBlendModeModulate = 4;
const int kMaterialTextureMaskHasOrm = 1;
const int kMaterialTextureMaskHasMetallic = 2;
const int kMaterialTextureMaskHasRoughness = 4;
const int kMaterialTextureMaskHasAmbientOcclusion = 8;

#ifndef MATERIAL_TWOSIDED
#define MATERIAL_TWOSIDED 0
#endif

#ifndef USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL
#define USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL 0
#endif

#ifndef MATERIAL_SHADINGMODEL_UNLIT
#define MATERIAL_SHADINGMODEL_UNLIT 0
#endif

#ifndef MATERIAL_SHADINGMODEL_SUBSURFACE
#define MATERIAL_SHADINGMODEL_SUBSURFACE 0
#endif

#ifndef MATERIAL_SHADINGMODEL_CLEAR_COAT
#define MATERIAL_SHADINGMODEL_CLEAR_COAT 0
#endif

float InterleavedGradientNoise(vec2 pixelPos)
{
    return fract(52.9829189 * fract(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}

vec3 RotateAroundY(vec3 dir, float degrees)
{
    float radians = degrees * (kPi / 180.0);
    float s = sin(radians);
    float c = cos(radians);
    return normalize(vec3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c
    ));
}

vec2 DirToEquirectUV(vec3 dir)
{
    dir = normalize(dir);
    float u = atan(dir.z, dir.x) / (2.0 * kPi) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / kPi;
    return vec2(u, v);
}

vec3 SampleIrradiance(vec3 dir)
{
    vec3 rotatedDir = RotateAroundY(normalize(dir), uLighting.iblRotationAndFlags.x);
    return texture(uIrradianceTex, DirToEquirectUV(rotatedDir)).rgb;
}

vec3 SamplePrefilteredEnvironment(vec3 dir, float roughness)
{
    vec3 rotatedDir = RotateAroundY(normalize(dir), uLighting.iblRotationAndFlags.x);
    vec2 baseUv = DirToEquirectUV(rotatedDir);
    float clampedRoughness = clamp(roughness, 0.0, 1.0);
    float level = clampedRoughness * (kPrefilterLevels - 1.0);
    float level0 = floor(level);
    float level1 = min(level0 + 1.0, kPrefilterLevels - 1.0);
    float blend = level - level0;
    float segmentHeight = 1.0 / kPrefilterLevels;
    vec2 uv0 = vec2(baseUv.x, (baseUv.y + level0) * segmentHeight);
    vec2 uv1 = vec2(baseUv.x, (baseUv.y + level1) * segmentHeight);
    return mix(
        texture(uPrefilteredEnvironmentTex, uv0).rgb,
        texture(uPrefilteredEnvironmentTex, uv1).rgb,
        blend);
}

vec3 ApplyLowerHemisphere(vec3 color, vec3 dir)
{
    if (uLighting.iblLowerHemisphereColorFlag.a > 0.5 && dir.y < 0.0)
    {
        return mix(color, uLighting.iblLowerHemisphereColorFlag.rgb, clamp(-dir.y, 0.0, 1.0));
    }
    return color;
}

float ComputeDistanceAttenuation(float distanceSq, float range)
{
    float rangeSq = max(range * range, 1.0e-4);
    float falloff = clamp(1.0 - distanceSq / rangeSq, 0.0, 1.0);
    return (falloff * falloff) / (1.0 + distanceSq);
}

float ComputeGodotOmniAttenuation(float distance, float range, float attenuation)
{
    float invRange = 1.0 / max(range, 1.0e-4);
    float nd = distance * invRange;
    nd *= nd;
    nd *= nd;
    nd = max(1.0 - nd, 0.0);
    nd *= nd;
    return nd * pow(max(distance, 1.0e-4), -max(attenuation, 1.0e-4));
}

float SampleShadowMap(sampler2D shadowMap, mat4 shadowMatrix, vec3 worldPos, float bias, float texelSize)
{
    vec4 shadowCoord = shadowMatrix * vec4(worldPos, 1.0);
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    vec3 projected = shadowCoord.xyz / shadowCoord.w;
    if (projected.x <= 0.0 || projected.x >= 1.0 || projected.y <= 0.0 || projected.y >= 1.0 || projected.z <= 0.0 || projected.z >= 1.0)
    {
        return 1.0;
    }

    float visibility = 0.0;
    for (int y = -1; y <= 0; ++y)
    {
        for (int x = -1; x <= 0; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float storedDepth = texture(shadowMap, projected.xy + offset).r;
            visibility += projected.z - bias <= storedDepth ? 1.0 : 0.0;
        }
    }

    return visibility * 0.25;
}

int SelectPointShadowFace(vec3 lightToSurface)
{
    vec3 absDir = abs(lightToSurface);
    if (absDir.x >= absDir.y && absDir.x >= absDir.z)
    {
        return lightToSurface.x >= 0.0 ? 0 : 1;
    }
    if (absDir.y >= absDir.x && absDir.y >= absDir.z)
    {
        return lightToSurface.y >= 0.0 ? 2 : 3;
    }
    return lightToSurface.z >= 0.0 ? 4 : 5;
}

float SamplePointShadow(vec3 worldPos)
{
    if (uLighting.pointShadowParams.x <= 0.5)
    {
        return 1.0;
    }

    vec3 lightToSurface = worldPos - uLighting.pointShadowLightPositionRange.xyz;
    float distanceToLight = length(lightToSurface);
    if (distanceToLight <= 1.0e-5 || distanceToLight >= uLighting.pointShadowLightPositionRange.w)
    {
        return 1.0;
    }

    int faceIndex = SelectPointShadowFace(lightToSurface);
    vec4 shadowCoord = uLighting.pointShadowMatrices[faceIndex] * vec4(worldPos, 1.0);
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    vec3 projected = shadowCoord.xyz / shadowCoord.w;
    if (projected.x <= 0.0 || projected.x >= 1.0 || projected.y <= 0.0 || projected.y >= 1.0 || projected.z <= 0.0 || projected.z >= 1.0)
    {
        return 1.0;
    }

    if (uLighting.pointShadowParams.w <= 0.5)
    {
        float storedDepth = texture(uPointShadowTex, projected.xy).r;
        return projected.z - uLighting.pointShadowParams.y <= storedDepth ? 1.0 : 0.0;
    }

    float visibility = 0.0;
    for (int y = -1; y <= 0; ++y)
    {
        for (int x = -1; x <= 0; ++x)
        {
            vec2 offset = vec2(float(x) * uLighting.pointShadowAtlasInvSize.x, float(y) * uLighting.pointShadowAtlasInvSize.y);
            float storedDepth = texture(uPointShadowTex, projected.xy + offset).r;
            visibility += projected.z - uLighting.pointShadowParams.y <= storedDepth ? 1.0 : 0.0;
        }
    }

    return visibility * 0.25;
}

mat3 ComputeCotangentFrame(vec3 normal, vec3 position, vec2 uv)
{
    vec3 dp1 = dFdx(position);
    vec3 dp2 = dFdy(position);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, normal);
    vec3 dp1perp = cross(normal, dp1);
    vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = inversesqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)) + 1.0e-8);
    return mat3(tangent * invMax, bitangent * invMax, normal);
}

float DistributionGGX(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1.0e-5);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return nDotV / max(nDotV * (1.0 - k) + k, 1.0e-5);
}

float GeometrySmith(float nDotV, float nDotL, float roughness)
{
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EnvBRDFApprox(vec3 f0, float roughness, float nDotV)
{
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

vec3 EvaluateDirectLight(
    vec3 lightRadiance,
    vec3 lightDir,
    vec3 viewDir,
    vec3 normal,
    vec3 albedo,
    vec3 f0,
    float roughness,
    float metallic,
    float nDotV)
{
    vec3 halfVector = normalize(lightDir + viewDir);
    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotH = max(dot(normal, halfVector), 0.0);
    float hDotV = max(dot(halfVector, viewDir), 0.0);
    vec3 fresnel = FresnelSchlick(hDotV, f0);
    float distribution = DistributionGGX(nDotH, roughness);
    float geometry = GeometrySmith(nDotV, nDotL, roughness);
    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 1.0e-4);
    vec3 kS = fresnel;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / kPi;
    return (diffuse + specular) * lightRadiance * nDotL;
}

void main()
{
    vec3 albedo = texture(uAlbedoTex, vUV).rgb * uPerDraw.tint.rgb;
    vec3 orm = texture(uORMTex, vUV).rgb;
    int textureMask = int(uPerDraw.materialParameters2.w + 0.5);
    vec3 emissiveSample = texture(uEmissiveTex, vUV).rgb;
    float textureOpacity = texture(uOpacityTex, vUV).r;
    float finalOpacity = clamp(uPerDraw.tint.a * uPerDraw.opacityAndNormal.x * textureOpacity, 0.0, 1.0);
    int blendMode = int(uPerDraw.uvTransform1.y + 0.5);
    if (blendMode == kBlendModeMasked && finalOpacity < uPerDraw.opacityAndNormal.y)
    {
        discard;
    }

    vec3 n = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
#if MATERIAL_TWOSIDED
    if (!gl_FrontFacing)
    {
        n = -n;
    }
#endif
    vec3 sampledNormal = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
    sampledNormal.xy *= max(uPerDraw.opacityAndNormal.z, 0.0);
    mat3 tbn = ComputeCotangentFrame(n, vWorldPos, vUV);
    n = normalize(tbn * sampledNormal);

    vec3 viewDir = normalize(uLighting.cameraWorldPosition.xyz - vWorldPos);
    vec3 directionalDir = normalize(-uLighting.directionalDirectionIntensity.xyz);
    vec3 directionalColor = uLighting.directionalColorEnabled.rgb;
    float directionalIntensity =
        uLighting.directionalDirectionIntensity.a * uLighting.directionalColorEnabled.a;
    float nDotV = max(dot(n, viewDir), 0.0);

    float roughnessSample = (textureMask & kMaterialTextureMaskHasRoughness) != 0 ? texture(uRoughnessTex, vUV).r : orm.g;
    float metallicSample = (textureMask & kMaterialTextureMaskHasMetallic) != 0 ? texture(uMetallicTex, vUV).r : orm.b;
    float aoSample = (textureMask & kMaterialTextureMaskHasAmbientOcclusion) != 0 ? texture(uAmbientOcclusionTex, vUV).r : orm.r;
    float roughness = clamp(max(roughnessSample, 0.04) * max(uPerDraw.surfaceParameters.y, 0.04), 0.04, 1.0);
    float metallic = clamp(metallicSample * uPerDraw.surfaceParameters.x, 0.0, 1.0);
    float ao = clamp(aoSample * uPerDraw.surfaceParameters.w, 0.0, 1.0);
    float specularStrength = clamp(uPerDraw.surfaceParameters.z, 0.0, 1.0);
    vec3 dielectricF0 = vec3(0.08 * specularStrength);
    vec3 f0 = mix(dielectricF0, albedo, metallic);

    float skyVisibility = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 fresnelView = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 ambient =
        uLighting.ambientColorIntensity.rgb *
        uLighting.ambientColorIntensity.a *
        ((vec3(1.0) - fresnelView) * (1.0 - metallic)) *
        albedo *
        ao *
        mix(0.42, 1.18, skyVisibility);
    vec3 bakedLighting =
        uPerDraw.lightmapParams.x > 0.5
            ? texture(uLightmapTex, vUV).rgb * albedo
            : vec3(0.0);

    vec3 directionalLight =
        EvaluateDirectLight(
            directionalColor * directionalIntensity,
            directionalDir,
            viewDir,
            n,
            albedo,
            f0,
            roughness,
            metallic,
            nDotV);
    if (uLighting.directionalShadowParams.x > 0.5)
    {
        float directionalShadow =
            SampleShadowMap(
                uDirectionalShadowTex,
                uLighting.directionalShadowMatrix,
                vWorldPos,
                uLighting.directionalShadowParams.y,
                uLighting.directionalShadowParams.z);
        directionalLight *= directionalShadow;
    }
    directionalLight *= max(uLighting.iblExposureAndSun.z, 0.0);

#if MATERIAL_SHADINGMODEL_SUBSURFACE
    {
        float transmission = max(dot(-n, directionalDir), 0.0);
        directionalLight +=
            directionalColor *
            (directionalIntensity * transmission * 0.2) *
            uPerDraw.subsurfaceAndCoat.rgb *
            (1.0 - metallic);
    }
#endif

#if MATERIAL_SHADINGMODEL_CLEAR_COAT
    {
        vec3 coatHalf = normalize(directionalDir + viewDir);
        float coatNDotL = max(dot(n, directionalDir), 0.0);
        float coatNDotH = max(dot(n, coatHalf), 0.0);
        float coatHDotV = max(dot(coatHalf, viewDir), 0.0);
        float coatStrength = clamp(uPerDraw.subsurfaceAndCoat.a, 0.0, 1.0);
        float coatRoughness = clamp(uPerDraw.materialParameters3.x, 0.04, 1.0);
        vec3 coatF = FresnelSchlick(coatHDotV, vec3(0.04));
        float coatD = DistributionGGX(coatNDotH, coatRoughness);
        float coatG = GeometrySmith(nDotV, coatNDotL, coatRoughness);
        vec3 coatSpec = (coatD * coatG * coatF) / max(4.0 * nDotV * coatNDotL, 1.0e-4);
        directionalLight += directionalColor * directionalIntensity * coatSpec * coatNDotL * coatStrength;
    }
#endif

    vec3 localLightAccum = vec3(0.0);
    int pointLightCount = int(uLighting.localLightCounts.x + 0.5);
    for (int index = 0; index < pointLightCount; ++index)
    {
        vec3 toLight = uLighting.pointPositionRange[index].xyz - vWorldPos;
        float distanceSq = dot(toLight, toLight);
        float attenuation = ComputeGodotOmniAttenuation(
            sqrt(max(distanceSq, 0.0)),
            uLighting.pointPositionRange[index].w,
            uLighting.pointColorIntensity[index].a);
        if (attenuation <= 0.0)
        {
            continue;
        }

        vec3 lightDir = normalize(toLight);
        vec3 lightRadiance = uLighting.pointColorIntensity[index].rgb * attenuation;
        if (uLighting.pointShadowParams.x > 0.5 &&
            abs(float(index) - uLighting.pointShadowParams.z) < 0.5)
        {
            lightRadiance *= SamplePointShadow(vWorldPos);
        }
        localLightAccum += EvaluateDirectLight(lightRadiance, lightDir, viewDir, n, albedo, f0, roughness, metallic, nDotV);
    }

    int spotLightCount = int(uLighting.localLightCounts.y + 0.5);
    for (int index = 0; index < spotLightCount; ++index)
    {
        vec3 toLight = uLighting.spotPositionRange[index].xyz - vWorldPos;
        float distanceSq = dot(toLight, toLight);
        float attenuation = ComputeDistanceAttenuation(distanceSq, uLighting.spotPositionRange[index].w);
        if (attenuation <= 0.0)
        {
            continue;
        }

        vec3 lightDir = normalize(toLight);
        vec3 surfaceDir = -lightDir;
        float coneFactor = smoothstep(
            uLighting.spotColorOuter[index].a,
            uLighting.spotDirectionInner[index].a,
            dot(surfaceDir, normalize(uLighting.spotDirectionInner[index].xyz)));
        if (coneFactor <= 0.0)
        {
            continue;
        }

        vec3 lightRadiance = uLighting.spotColorOuter[index].rgb * (attenuation * coneFactor);
        if (uLighting.spotShadowParams.x > 0.5 &&
            abs(float(index) - uLighting.spotShadowParams.w) < 0.25)
        {
            float spotShadow =
                SampleShadowMap(
                    uSpotShadowTex,
                    uLighting.spotShadowMatrix,
                    vWorldPos,
                    uLighting.spotShadowParams.y,
                    uLighting.spotShadowParams.z);
            lightRadiance *= spotShadow;
        }
        localLightAccum += EvaluateDirectLight(lightRadiance, lightDir, viewDir, n, albedo, f0, roughness, metallic, nDotV);
    }

    vec3 iblDiffuseColor = uLighting.iblDiffuseColorIntensity.rgb;
    vec3 iblSpecularColor = uLighting.iblSpecularColorIntensity.rgb;
    if (uLighting.iblRotationAndFlags.y > 0.5)
    {
        iblDiffuseColor *= SampleIrradiance(n);
        vec3 reflectionDir = reflect(-viewDir, n);
        iblSpecularColor *= SamplePrefilteredEnvironment(reflectionDir, roughness);
        iblSpecularColor = ApplyLowerHemisphere(iblSpecularColor, reflectionDir);
    }
    iblDiffuseColor = ApplyLowerHemisphere(iblDiffuseColor, n);

    vec3 kS = fresnelView;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 iblDiffuse =
        iblDiffuseColor *
        (uLighting.iblDiffuseColorIntensity.a * ao) *
        (kD * albedo) *
        mix(0.75, 1.2, skyVisibility);
    vec3 envBrdf = EnvBRDFApprox(f0, roughness, nDotV);
    vec3 iblSpecular =
        iblSpecularColor *
        (uLighting.iblSpecularColorIntensity.a * ao * uLighting.iblParams.x) *
        envBrdf;

#if MATERIAL_SHADINGMODEL_CLEAR_COAT
    {
        float coatStrength = clamp(uPerDraw.subsurfaceAndCoat.a, 0.0, 1.0);
        float coatRoughness = clamp(uPerDraw.materialParameters3.x, 0.04, 1.0);
        vec3 reflectionDir = reflect(-viewDir, n);
        vec3 coatEnv = SamplePrefilteredEnvironment(reflectionDir, coatRoughness);
        coatEnv = ApplyLowerHemisphere(coatEnv, reflectionDir);
        vec3 coatBrdf = EnvBRDFApprox(vec3(0.04), coatRoughness, nDotV);
        iblSpecular += coatEnv * coatBrdf * coatStrength * uLighting.iblSpecularColorIntensity.a;
    }
#endif

    vec3 emissive =
        emissiveSample *
        uPerDraw.emissiveColorIntensity.rgb *
        max(uPerDraw.emissiveColorIntensity.a, 0.0);
    vec3 litColor =
        vColor *
        (ambient + bakedLighting + directionalLight + localLightAccum + iblDiffuse + iblSpecular);
    vec3 color = litColor;
#if MATERIAL_SHADINGMODEL_UNLIT
    color = albedo * vColor;
#endif
    float outputAlpha = 1.0;
    color += emissive;
    if (blendMode == kBlendModeTranslucent)
    {
        outputAlpha = finalOpacity;
    }
    else if (blendMode == kBlendModeAdditive || blendMode == kBlendModeModulate)
    {
        color *= finalOpacity;
    }
#if USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL
    if (finalOpacity < 0.999 && InterleavedGradientNoise(gl_FragCoord.xy) > finalOpacity)
    {
        discard;
    }
#endif
    outColor = vec4(max(color, vec3(0.0)), outputAlpha);
}
