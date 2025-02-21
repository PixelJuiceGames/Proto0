#include "ShaderGlobals.h"
#include "Common.hlsli"

Texture2D<float4> gSceneColor : register(t0, SPACE_PerFrame);
SamplerState gLinearClampSampler : register(s0, SPACE_Persistent);

[RootSignature(DefaultRootSignature)]
float4 main(FullscreenVaryings varyings) : SV_TARGET
{
    float3 hdrColor = gSceneColor.Sample(gLinearClampSampler, varyings.UV).rgb;
    return float4(hdrColor, 1.0f);
}