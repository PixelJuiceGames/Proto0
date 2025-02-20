#include "UberResources.hlsli"

[RootSignature(DefaultRootSignature)]
Varyings main(Attributes attributes)
{
    Varyings varyings = (Varyings) 0;
    varyings.Color = attributes.Color;
    varyings.Texcoord0 = attributes.Texcoord0;
    varyings.PositionWS = attributes.Position;
    varyings.PositionCS = mul(g_Frame.projViewMat, float4(attributes.Position, 1.0));

    return varyings;
}