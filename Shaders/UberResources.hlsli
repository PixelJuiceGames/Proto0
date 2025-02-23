#ifndef _COMMON_HLSLI
#define _COMMON_HLSLI

#include "ShaderGlobals.h"

struct Varyings
{
    float4 PositionCS : SV_Position;
    float3 PositionWS : POSITION;
    float3 NormalWS : NORMAL;
    float4 TangentWS : TANGENT;
    float3 Color : COLOR;
    float2 Texcoord0 : TEXCOORD0;
    uint instanceID : SV_InstanceID;
};

SamplerState gLinearRepeatSampler : register(s0, SPACE_Persistent);

cbuffer CB0 : register(b0, SPACE_PerFrame)
{
    Frame g_Frame;
};

#endif // _COMMON_HLSLI