#ifndef _COMMON_HLSLI
#define _COMMON_HLSLI

struct FullscreenVaryings
{
    float4 PositionCS : SV_Position;
    float2 UV : TEXCOORD0;
};

#endif // _COMMON_HLSLI