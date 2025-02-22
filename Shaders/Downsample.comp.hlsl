// Based on Froyok Lena Piquet bloom article
// https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
#include "ShaderGlobals.h"

Texture2D<float4> gSourceTexture : register(t1, SPACE_PerDraw);
RWTexture2D<float4> gDestinationTexture : register(u2, SPACE_PerDraw);
SamplerState gLinearClampSampler : register(s0, SPACE_Persistent);

cbuffer CB0 : register(b0, SPACE_PerDraw)
{
    DownsampleUniform g_DownsampleUniform;
};

float3 Downsample(float2 uv, float2 pixelSize)
{
    const float2 coords[13] =
    {
        float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-2.0f,  2.0f), float2( 0.0f,  2.0f), float2( 2.0f,  2.0f),
        float2(-2.0f,  0.0f), float2( 0.0f,  0.0f), float2( 2.0f,  0.0f),
        float2(-2.0f, -2.0f), float2( 0.0f, -2.0f), float2( 2.0f, -2.0f)
    };
    
    const float weights[13] =
    {
        // 4 samples
        // (1 / 4) * 0.5f
        0.125f, 0.125f,
        0.125f, 0.125f,
        
        // 9 samples
        // (1 / 9) * 0.5f
        0.0555555f, 0.0555555f, 0.0555555f,
        0.0555555f, 0.0555555f, 0.0555555f,
        0.0555555f, 0.0555555f, 0.0555555f
    };
    
    float3 outColor = 0;
    
    [unroll]
    for (uint i = 0; i < 13; i++)
    {
        float2 currentUV = uv + coords[i] * pixelSize;
        outColor += weights[i] * gSourceTexture.SampleLevel(gLinearClampSampler, currentUV, 0).rgb;
    }
    
    return outColor;
}

[RootSignature(ComputeRootSignature)]
[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint width;
    uint height;
    gDestinationTexture.GetDimensions(width,  height);
    
    if (DTid.x < width && DTid.y < height)
    {   
        float2 inPixelSize = (1.0f / float2(g_DownsampleUniform.inputSize)) * 0.5f;
        float2 uv = float2(DTid.xy + 0.5f) / float2(width, height);
        float3 downsampledColor = Downsample(uv, inPixelSize);
    
        gDestinationTexture[DTid.xy] = float4(downsampledColor, 1.0f);
    }
}