#include "ShaderGlobals.h"

Texture2D<float4> gSourceTexture : register(t1, SPACE_PerDraw);
Texture2D<float4> gPreviousTexture : register(t2, SPACE_PerDraw);
RWTexture2D<float4> gDestinationTexture : register(u3, SPACE_PerDraw);
SamplerState gLinearClampSampler : register(s0, SPACE_Persistent);

cbuffer CB0 : register(b0, SPACE_PerDraw)
{
    UpsampleUniform g_UpsampleUniform;
};

float3 Upsample(float2 uv, float2 pixelSize)
{
    const float2 coords[9] =
    {
        float2(-1.0f,  1.0f), float2( 0.0f,  1.0f), float2( 1.0f,  1.0f),
        float2(-1.0f,  0.0f), float2( 0.0f,  0.0f), float2( 1.0f,  0.0f),
        float2(-1.0f, -1.0f), float2( 0.0f, -1.0f), float2( 1.0f, -1.0f)
    };
    
    const float weights[9] =
    {
        0.0625f, 0.125f, 0.0625f,
        0.125f,  0.25f,  0.125f,
        0.0625f, 0.125f, 0.0625f
    };
    
    float3 outColor = 0;
    
    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        float2 currentUV = uv + coords[i] * pixelSize;
        outColor += weights[i] * gPreviousTexture.SampleLevel(gLinearClampSampler, currentUV, 0).rgb;
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
        float2 inPixelSize = (1.0f / float2(g_UpsampleUniform.inputSize)) * 0.5f;
        float2 uv = float2(DTid.xy + 0.5f) / float2(width, height);
        float3 previousColor = Upsample(uv, inPixelSize);
        float3 currentColor = gSourceTexture[DTid.xy].rgb;
    
        gDestinationTexture[DTid.xy] = float4(lerp(currentColor, previousColor, g_UpsampleUniform.radius), 1.0f);
    }
}