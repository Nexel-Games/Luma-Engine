#version 450

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 1) uniform sampler2D uSceneColorTex;
layout(set = 0, binding = 4) uniform LightingData
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
} uLighting;

layout(location = 0) out vec4 outColor;

vec3 ACESFilm(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 ACESFilmWithCurve(vec3 color, float shoulder, float linearScale, float toe)
{
    float a = 2.51 * max(shoulder, 0.1);
    float b = 0.03 * max(toe, 0.1);
    float c = 2.43 * max(shoulder, 0.1);
    float d = 0.59 * max(linearScale, 0.1);
    float e = 0.14 * max(toe, 0.1);
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

float ComputeLuminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 ReinhardToneMap(vec3 color, float whitePoint)
{
    vec3 scaled = max(color, vec3(0.0));
    float whitePointSq = max(whitePoint * whitePoint, 1.0e-4);
    return clamp((scaled * (vec3(1.0) + scaled / whitePointSq)) / (vec3(1.0) + scaled), 0.0, 1.0);
}

vec3 SampleSceneHDR(vec2 uv)
{
    return max(texture(uSceneColorTex, uv).rgb, vec3(0.0));
}

vec3 ApplySoftThreshold(vec3 color, float threshold, float knee)
{
    float brightness = ComputeLuminance(color);
    float safeKnee = max(knee, 1.0e-4);
    float soft = clamp(brightness - threshold + safeKnee, 0.0, 2.0 * safeKnee);
    soft = (soft * soft) / max(4.0 * safeKnee, 1.0e-4);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1.0e-4);
    return color * max(contribution, 0.0);
}

vec3 SampleBloom(vec2 uv)
{
    if (uLighting.postProcessBloom.x <= 0.5 || uLighting.postProcessBloom.y <= 0.0)
    {
        return vec3(0.0);
    }

    vec2 texelSize = 1.0 / max(vec2(textureSize(uSceneColorTex, 0)), vec2(1.0));
    float threshold = max(uLighting.postProcessBloom.z, 0.0);
    float knee = max(uLighting.postProcessBloom.w, 1.0e-4);

    vec3 bloom = ApplySoftThreshold(SampleSceneHDR(uv), threshold, knee) * 0.24;
    bloom += ApplySoftThreshold(SampleSceneHDR(uv + vec2(texelSize.x, 0.0)), threshold, knee) * 0.16;
    bloom += ApplySoftThreshold(SampleSceneHDR(uv - vec2(texelSize.x, 0.0)), threshold, knee) * 0.16;
    bloom += ApplySoftThreshold(SampleSceneHDR(uv + vec2(0.0, texelSize.y)), threshold, knee) * 0.16;
    bloom += ApplySoftThreshold(SampleSceneHDR(uv - vec2(0.0, texelSize.y)), threshold, knee) * 0.16;
    bloom += ApplySoftThreshold(SampleSceneHDR(uv + texelSize), threshold, knee) * 0.06;
    bloom += ApplySoftThreshold(SampleSceneHDR(uv - texelSize), threshold, knee) * 0.06;
    return bloom * uLighting.postProcessBloom.y;
}

vec3 ApplyOutputTransform(vec3 color)
{
    color *= max(uLighting.iblExposureAndSun.x, 1.0e-4);

    if (uLighting.postProcessToneMap.x > 0.5)
    {
        color *= exp2(uLighting.postProcessToneMap.w + uLighting.postProcessColorBalance.a);
        color *= max(uLighting.postProcessColorBalance.rgb, vec3(0.0));
        color *= max(uLighting.postProcessColor.rgb, vec3(0.0));

        float saturation = max(uLighting.postProcessColor.a, 0.0);
        float luminance = ComputeLuminance(color);
        color = mix(vec3(luminance), color, saturation);

        float contrast = max(uLighting.postProcessCurve.x, 0.0);
        color = (color - vec3(0.5)) * contrast + vec3(0.5);
        color = max(color, vec3(0.0));

        float whitePoint = max(uLighting.postProcessCurve.z, 1.0e-4);
        if (uLighting.postProcessToneMap.y > 0.5)
        {
            int toneMappingOperator = int(floor(uLighting.postProcessToneMap.z + 0.5));
            if (toneMappingOperator == 0)
            {
                color = clamp(color / whitePoint, 0.0, 1.0);
            }
            else if (toneMappingOperator == 1)
            {
                color = ReinhardToneMap(color, whitePoint);
            }
            else
            {
                color = ACESFilmWithCurve(
                    color / whitePoint,
                    uLighting.postProcessFilmCurve.x,
                    uLighting.postProcessFilmCurve.y,
                    uLighting.postProcessFilmCurve.z);
            }
        }
        else
        {
            color = clamp(color / whitePoint, 0.0, 1.0);
        }

        float gamma = max(uLighting.postProcessCurve.y, 1.0e-4);
        color = pow(max(color, vec3(0.0)), vec3(1.0 / gamma));
        return clamp(color, 0.0, 1.0);
    }

    return ACESFilm(color);
}

void main()
{
    vec3 hdrColor = SampleSceneHDR(vUV) + SampleBloom(vUV);
    outColor = vec4(ApplyOutputTransform(hdrColor), 1.0);
}
