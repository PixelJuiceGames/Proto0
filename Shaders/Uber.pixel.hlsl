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
    
    return albedo;
}