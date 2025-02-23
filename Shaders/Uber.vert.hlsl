#include "UberResources.hlsli"

[RootSignature(DefaultRootSignature)]
Varyings main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID, uint startVertexLocation : SV_StartVertexLocation, uint startInstanceLocation : SV_StartInstanceLocation)
{
    uint instanceIndex = instanceID + startInstanceLocation;
    ByteAddressBuffer instanceBuffer = ResourceDescriptorHeap[g_Frame.instanceBufferIndex];
    GPUInstance instance = instanceBuffer.Load<GPUInstance>(instanceIndex * sizeof(GPUInstance));
    
    uint vertexIndex = vertexID + startVertexLocation;
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[g_Frame.vertexBufferIndex];
    MeshVertex vertex = vertexBuffer.Load<MeshVertex>(vertexIndex * sizeof(MeshVertex));
    
    Varyings varyings = (Varyings) 0;
    varyings.Color = vertex.color;
    varyings.Texcoord0 = vertex.uv;
    varyings.PositionWS = mul(instance.worldMat, float4(vertex.position, 1.0)).xyz;
    varyings.PositionCS = mul(g_Frame.projViewMat, float4(varyings.PositionWS, 1.0));
    varyings.NormalWS = normalize(mul((float3x3) instance.worldMat, vertex.normal));
    varyings.TangentWS.xyz = normalize(mul((float3x3) instance.worldMat, vertex.tangent.xyz));
    varyings.TangentWS.w = -vertex.tangent.w;
    varyings.instanceID = instanceIndex;

    return varyings;
}