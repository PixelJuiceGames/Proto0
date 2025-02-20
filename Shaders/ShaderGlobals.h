#ifndef _SHADER_GLOBALS
#define _SHADER_GLOBALS

#define SET_Persistent 0
#define SET_PerFrame   1
#define SET_PerBatch   2
#define SET_PerDraw    3

#define SPACE_Persistent space0
#define SPACE_PerFrame   space1
#define SPACE_PerBatch   space2
#define SPACE_PerDraw    space3

#define ROOT_PARAM_Persistent_SAMPLER 4
#define ROOT_PARAM_Persistent         3
#define ROOT_PARAM_PerFrame           2
#define ROOT_PARAM_PerBatch           1
#define ROOT_PARAM_PerDraw            0

#if !defined(__cplusplus)

#define DESCRIPTOR_TABLE(space)                                             \
    "DescriptorTable("                                                      \
    "SRV(t0, numDescriptors = unbounded, space = " #space ", offset = 0),"     \
    "CBV(b0, numDescriptors = unbounded, space = " #space ", offset = 0),"     \
    "UAV(u0, numDescriptors = unbounded, space = " #space ", offset = 0)),"

#define SAMPLER_TABLE(space)                                                \
    "DescriptorTable("                                                      \
    "SAMPLER(s0, numDescriptors = unbounded, space = " #space ", offset = 0))"

#define DefaultRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," DESCRIPTOR_TABLE(3) DESCRIPTOR_TABLE(2) DESCRIPTOR_TABLE(1) DESCRIPTOR_TABLE(0) SAMPLER_TABLE(0)

#endif

#if defined(__cplusplus)
typedef struct { float m[16]; } float4x4;
#endif

struct Frame
{
	float4x4 projViewMat;
};

#endif // _SHADER_GLOBALS
