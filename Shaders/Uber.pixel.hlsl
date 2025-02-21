#include "UberResources.hlsli"

[RootSignature(DefaultRootSignature)]
float4 main(Varyings varyings) : SV_Target0
{
    ByteAddressBuffer instanceBuffer = ResourceDescriptorHeap[g_Frame.instanceBufferIndex];
    GPUInstance instance = instanceBuffer.Load<GPUInstance>(varyings.instanceID * sizeof(GPUInstance));
    
    ByteAddressBuffer materialBuffer = ResourceDescriptorHeap[g_Frame.materialBufferIndex];
    GPUMaterial material = materialBuffer.Load<GPUMaterial>(instance.materialBufferOffset);
    
    // NOTE: colors sent to the shader are already in linear format
    float4 albedo = material.baseColor;
    
    if (HasValidTexture(material.albedoTextureIndex))
    {
        Texture2D albedoTexture = ResourceDescriptorHeap[material.albedoTextureIndex];
        float4 albedoSample = albedoTexture.Sample(gLinearRepeatSampler, varyings.Texcoord0);
        albedo.rgb *= albedoSample.rgb;
    }
    
    float3 emission = 0;
    if (HasValidTexture(material.emissiveTextureIndex))
    {
        Texture2D emissiveTexture = ResourceDescriptorHeap[material.emissiveTextureIndex];
        float4 emissiveSample = emissiveTexture.Sample(gLinearRepeatSampler, varyings.Texcoord0);
        emission += emissiveSample.rgb;
    }
    
    float3 N = normalize(varyings.NormalWS);
    float3 L = -normalize(g_Frame.sunDirection.xyz);
    float NdotL = max(0.0f, dot(N, L));
    albedo.rgb *= NdotL * g_Frame.sunColor.rgb * g_Frame.sunColor.a;
    albedo.rgb += emission;
    
    return albedo;
}