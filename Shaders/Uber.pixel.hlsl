#include "UberResources.hlsli"
#include "PBR.hlsli"

[RootSignature(DefaultRootSignature)]
GBufferOutput main(Varyings varyings)
{
    ByteAddressBuffer instanceBuffer = ResourceDescriptorHeap[g_Frame.instanceBufferIndex];
    GPUInstance instance = instanceBuffer.Load<GPUInstance>(varyings.instanceID * sizeof(GPUInstance));
    
    ByteAddressBuffer materialBuffer = ResourceDescriptorHeap[g_Frame.materialBufferIndex];
    GPUMaterial material = materialBuffer.Load<GPUMaterial>(instance.materialBufferIndex * sizeof(GPUMaterial));
    
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
    
    GBufferOutput output = (GBufferOutput)0;
    output.GBuffer0 = float4(albedo.rgb, 1.0f);
    output.GBuffer1 = float4(N, 0.0f);
    output.GBuffer2 = float4(occlusion, roughness, metalness, material.reflectance);
    output.GBuffer3 = float4(emission, material.emissiveFactor);
    
    return output;
}