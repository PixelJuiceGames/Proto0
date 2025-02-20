#ifndef _UBER_RESOURCES_HLSLI
#define _UBER_RESOURCES_HLSLI

#include "ShaderGlobals.h"

struct Attributes
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Color : COLOR;
    float2 Texcoord0 : TEXCOORD0;
};

struct Varyings
{
    float4 PositionCS : SV_Position;
    float3 PositionWS : POSITION;
    float3 NormalWS : NORMAL;
    float3 Color : COLOR;
    float2 Texcoord0 : TEXCOORD0;
};

cbuffer CB0 : register(b0, SPACE_PerFrame)
{
    Frame g_Frame;
};

//cbuffer g_RootConstant : register(b1, UPDATE_FREQ_PER_DRAW)
//{
//
//}

#endif // _UBER_RESOURCES_HLSLI