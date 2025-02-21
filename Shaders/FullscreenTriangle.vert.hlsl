#include "ShaderGlobals.h"
#include "Common.hlsli"

[RootSignature(DefaultRootSignature)]
FullscreenVaryings main(uint vertexID : SV_VertexID) 
{
    FullscreenVaryings varyings = (FullscreenVaryings) 0;
    varyings.UV = float2((vertexID << 1) & 2, vertexID & 2);
    varyings.PositionCS = float4(varyings.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return varyings;
}