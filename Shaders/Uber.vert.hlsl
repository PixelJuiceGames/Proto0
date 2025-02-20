#include "UberResources.hlsli"

[RootSignature(DefaultRootSignature)]
Varyings main(uint vertexID : SV_VertexID)
{
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[g_Frame.vertexBufferIndex];
    MeshVertex vertex = vertexBuffer.Load<MeshVertex>(vertexID * sizeof(MeshVertex));
    
    Varyings varyings = (Varyings) 0;
    varyings.Color = vertex.color;
    varyings.Texcoord0 = vertex.uv;
    varyings.PositionWS = vertex.position;
    varyings.PositionCS = mul(g_Frame.projViewMat, float4(vertex.position, 1.0));

    return varyings;
}