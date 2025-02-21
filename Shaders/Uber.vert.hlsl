#include "UberResources.hlsli"

[RootSignature(DefaultRootSignature)]
Varyings main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[g_Frame.vertexBufferIndex];
    MeshVertex vertex = vertexBuffer.Load<MeshVertex>(vertexID * sizeof(MeshVertex));
    
    ByteAddressBuffer instanceBuffer = ResourceDescriptorHeap[g_Frame.instanceBufferIndex];
    GPUInstance instance = instanceBuffer.Load<GPUInstance>(instanceID * sizeof(GPUInstance));
    
    Varyings varyings = (Varyings) 0;
    varyings.Color = vertex.color;
    varyings.Texcoord0 = vertex.uv;
    varyings.PositionWS = mul(instance.worldMat, float4(vertex.position, 1.0)).xyz;
    varyings.PositionCS = mul(g_Frame.projViewMat, float4(varyings.PositionWS, 1.0));
    varyings.NormalWS = normalize(mul((float3x3) instance.worldMat, vertex.normal));
    varyings.instanceID = instanceID;

    return varyings;
}