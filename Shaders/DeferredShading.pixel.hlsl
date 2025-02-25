#include "ShaderGlobals.h"
#include "Common.hlsli"
#include "PBR.hlsli"

SamplerState gLinearClampSampler : register(s0, SPACE_Persistent);

cbuffer CB0 : register(b0, SPACE_PerFrame)
{
    Frame g_Frame;
};

Texture2D<float4> gGBuffer0 : register(t1, SPACE_PerFrame);
Texture2D<float4> gGBuffer1 : register(t2, SPACE_PerFrame);
Texture2D<float4> gGBuffer2 : register(t3, SPACE_PerFrame);
Texture2D<float4> gGBuffer3 : register(t4, SPACE_PerFrame);
Texture2D<float> gDepthBuffer : register(t5, SPACE_PerFrame);


float4 GetClipPositionFromDepth(float depth, float2 uv)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = (1.0f - uv.y) * 2.0f - 1.0f;
    float4 positionCS = float4(x, y, depth, 1.0f);
    return mul(g_Frame.invProjViewMat, positionCS);
}

float3 GetWorldPositionFromDepth(float depth, float2 uv)
{
    float4 positionCS = GetClipPositionFromDepth(depth, uv);
    return positionCS.xyz / positionCS.w;
}

[RootSignature(DefaultRootSignature)]
float4 main(FullscreenVaryings varyings) : SV_TARGET
{
    const float depth = gDepthBuffer.SampleLevel(gLinearClampSampler, varyings.UV, 0).r;
    
    const float3 P = GetWorldPositionFromDepth(depth, varyings.UV);
    const float3 V = normalize(g_Frame.cameraPosition.xyz - P);
    const float3 N = gGBuffer1.SampleLevel(gLinearClampSampler, varyings.UV, 0).rgb;
    const float3 albedo = gGBuffer0.SampleLevel(gLinearClampSampler, varyings.UV, 0).rgb;
    const float4 ORM = gGBuffer2.SampleLevel(gLinearClampSampler, varyings.UV, 0);
    const float occlusion = ORM.r;
    const float roughness = ORM.g;
    const float metalness = ORM.b;
    const float reflectance = ORM.a;
    const float3 emission = gGBuffer3.SampleLevel(gLinearClampSampler, varyings.UV, 0).rgb;
    
    float3 Lo = 0.0f;
    
    // Sun light
    {
        float3 L = -normalize(g_Frame.sunDirection.xyz);
        float NdotL = max(0.00001f, dot(N, L));
        float3 radiance = g_Frame.sunColor.rgb * g_Frame.sunColor.a;
        const float3 brdf = FilamentBRDF(N, V, L, albedo.rgb, roughness, metalness, reflectance);
        Lo += brdf * radiance * NdotL * occlusion;
    }
    
    // Point lights
    {
        ByteAddressBuffer lightBuffer = ResourceDescriptorHeap[g_Frame.lightBufferIndex];
        
        for (uint i = 0; i < g_Frame.numLights; i++)
        {
            GPULight light = lightBuffer.Load<GPULight>(i * sizeof(GPULight));
            float3 L = normalize(light.position - P);
            float NdotL = max(0.00001f, dot(N, L));
            float distance = length(light.position - P);
            float distanceRadius = 1.0f - pow(distance / light.range, 4);
            float clamped = pow(saturate(distanceRadius), 2);
            float attenuation = clamped / (distance * distance + 1.0f);
            float3 radiance = light.color * light.intensity * attenuation;
            const float3 brdf = FilamentBRDF(N, V, L, albedo.rgb, roughness, metalness, reflectance);
            Lo += brdf * radiance * NdotL * occlusion;
            
        }
    }
    
    // Emissions
    Lo += emission.rgb;
    
    return float4(Lo, 1.0f);
}