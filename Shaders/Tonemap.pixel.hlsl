#include "ShaderGlobals.h"
#include "Common.hlsli"

Texture2D<float4> gSceneColor : register(t0, SPACE_PerFrame);
Texture3D<float3> gTonyMcMapfaceLut : register(t1, SPACE_PerFrame);
Texture2D<float4> gBloomBuffer : register(t2, SPACE_PerFrame);
SamplerState gLinearClampSampler : register(s0, SPACE_Persistent);

// https://github.com/h3r2tic/tony-mc-mapface
// MIT License
float3 tony_mc_mapface(float3 stimulus)
{
    // Apply a non-linear transform that the LUT is encoded with.
    const float3 encoded = stimulus / (stimulus + 1.0);

    // Align the encoded range to texel centers.
    const float LUT_DIMS = 48.0;
    const float3 uv = encoded * ((LUT_DIMS - 1.0) / LUT_DIMS) + 0.5 / LUT_DIMS;

    // Note: for OpenGL, do `uv.y = 1.0 - uv.y`

    return gTonyMcMapfaceLut.SampleLevel(gLinearClampSampler, uv, 0);
}

[RootSignature(DefaultRootSignature)]
float4 main(FullscreenVaryings varyings) : SV_TARGET
{
    float3 hdrColor = gSceneColor.Sample(gLinearClampSampler, varyings.UV).rgb;
    float3 bloom = gBloomBuffer.Sample(gLinearClampSampler, varyings.UV).rgb;
    float3 sdrColor = tony_mc_mapface(hdrColor + bloom);
    return float4(sdrColor, 1.0f);
}