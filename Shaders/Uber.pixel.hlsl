#include "UberResources.hlsli"
#include "PBR.hlsli"

[RootSignature(DefaultRootSignature)]
float4 main(Varyings varyings) : SV_Target0
{
    ByteAddressBuffer instanceBuffer = ResourceDescriptorHeap[g_Frame.instanceBufferIndex];
    GPUInstance instance = instanceBuffer.Load<GPUInstance>(varyings.instanceID * sizeof(GPUInstance));
    
    ByteAddressBuffer materialBuffer = ResourceDescriptorHeap[g_Frame.materialBufferIndex];
    GPUMaterial material = materialBuffer.Load<GPUMaterial>(instance.materialBufferOffset);
    
    float2 uv0 = varyings.Texcoord0 * material.uv0Tiling;
    
    // NOTE: colors sent to the shader are already in linear format
    float4 albedo = material.baseColor;
    
    if (HasValidTexture(material.albedoTextureIndex))
    {
        Texture2D albedoTexture = ResourceDescriptorHeap[material.albedoTextureIndex];
        float4 albedoSample = albedoTexture.Sample(gLinearRepeatSampler, uv0);
        albedo.rgb *= albedoSample.rgb;
    }
    
    float3 N = normalize(varyings.NormalWS);
    if (HasValidTexture(material.normalTextureIndex))
    {
        Texture2D normalTexture = ResourceDescriptorHeap[material.normalTextureIndex];
        float4 normalSample = normalTexture.Sample(gLinearRepeatSampler, uv0);
        normalSample.xy = (normalSample.xy * 2.0f - 1.0f) * material.normalIntensity;
        normalSample.z = normalize(sqrt(1.0f - saturate(dot(normalSample.xy, normalSample.xy))));
        
        float3 T = normalize(varyings.TangentWS.xyz);
        float3 B = normalize(cross(N, T)) * varyings.TangentWS.w;
        // NOTE(gmodarelli): Figure out why we need to transpose here
        float3x3 TBN = transpose(float3x3(T, B, N));
        
        N = normalize(mul(TBN, normalSample.xyz));
    }
    
    float occlusion = material.occlusionFactor;
    float roughness = material.roughnessFactor;
    float metalness = material.metalnessFactor;
    
    if (HasValidTexture(material.ormTextureIndex))
    {
        Texture2D ormTexture = ResourceDescriptorHeap[material.ormTextureIndex];
        float4 ormSample = ormTexture.Sample(gLinearRepeatSampler, uv0);
        occlusion *= ormSample.r;
        roughness *= ormSample.g;
        metalness *= ormSample.b;
    }
    
    float3 emission = 0;
    if (HasValidTexture(material.emissiveTextureIndex))
    {
        Texture2D emissiveTexture = ResourceDescriptorHeap[material.emissiveTextureIndex];
        float4 emissiveSample = emissiveTexture.Sample(gLinearRepeatSampler, uv0);
        emission += emissiveSample.rgb;
    }
    
    float3 V = normalize(g_Frame.cameraPosition.xyz - varyings.PositionWS);
    float3 Lo = 0.0f;
    
    // Sun light
    {
        float3 L = -normalize(g_Frame.sunDirection.xyz);
        float NdotL = max(0.00001f, dot(N, L));
        float3 radiance = g_Frame.sunColor.rgb * g_Frame.sunColor.a;
        const float3 brdf = FilamentBRDF(N, V, L, albedo.rgb, roughness, metalness, material.reflectance);
        Lo += brdf * radiance * NdotL * occlusion;
    }
    
    // Point lights
    {
        ByteAddressBuffer lightBuffer = ResourceDescriptorHeap[g_Frame.lightBufferIndex];
        
        for (uint i = 0; i < g_Frame.numLights; i++)
        {
            GPULight light = lightBuffer.Load<GPULight>(i * sizeof(GPULight));
            float3 L = normalize(light.position - varyings.PositionWS);
            float NdotL = max(0.00001f, dot(N, L));
            float distance = length(light.position - varyings.PositionWS);
            float distanceRadius = 1.0f - pow(distance / light.range, 4);
            float clamped = pow(saturate(distanceRadius), 2);
            float attenuation = clamped / (distance * distance + 1.0f);
            float3 radiance = light.color * light.intensity * attenuation;
            const float3 brdf = FilamentBRDF(N, V, L, albedo.rgb, roughness, metalness, material.reflectance);
            Lo += brdf * radiance * NdotL * occlusion;
        }
    }
    
    // Emissions
    Lo += (emission * material.emissiveFactor);
    
    return float4(Lo, 1.0f);
}