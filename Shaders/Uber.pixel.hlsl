#include "UberResources.hlsli"

float4 main(Varyings varyings) : SV_Target0
{
    return float4(varyings.Color, 1.0);
}