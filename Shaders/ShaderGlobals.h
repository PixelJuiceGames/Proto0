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

#define DESCRIPTOR_TABLE(space)                                            \
    "DescriptorTable("                                                     \
    "SRV(t0, numDescriptors = unbounded, space = " #space ", offset = 0)," \
    "CBV(b0, numDescriptors = unbounded, space = " #space ", offset = 0)," \
    "UAV(u0, numDescriptors = unbounded, space = " #space ", offset = 0)),"

#define SAMPLER_DESCRIPTOR_TABLE(space) \
    "DescriptorTable("                  \
    "SAMPLER(s0, numDescriptors = unbounded, space = " #space ", offset = 0)),"

#define DefaultRootSignature                                                                                                         \
    "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED),"                                                                                  \
        DESCRIPTOR_TABLE(3) DESCRIPTOR_TABLE(2) DESCRIPTOR_TABLE(1) DESCRIPTOR_TABLE(0)                                              \
        SAMPLER_DESCRIPTOR_TABLE(                                                                                                    \
            0) "StaticSampler(s0, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_POINT,"                                                                                  \
               "addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP),"              \
               "StaticSampler(s1, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_POINT,"                                                                                  \
               "addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP),"                 \
               "StaticSampler(s2, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT,"                                                                           \
               "addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP),"              \
               "StaticSampler(s3, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT,"                                                                           \
               "addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP),"                 \
               "StaticSampler(s4, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_LINEAR,"                                                                                 \
               "addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP),"              \
               "StaticSampler(s5, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_LINEAR,"                                                                                 \
               "addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP),"                 \
               "StaticSampler(s6, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_POINT,"                                                                                  \
               "addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR),"           \
               "StaticSampler(s7, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_POINT, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK,"                             \
               "addressU = TEXTURE_ADDRESS_BORDER, addressV = TEXTURE_ADDRESS_BORDER, addressW = TEXTURE_ADDRESS_BORDER),"           \
               "StaticSampler(s8, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_LINEAR,"                                                                                 \
               "addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR),"           \
               "StaticSampler(s9, space = 100,"                                                                                      \
               "filter = FILTER_MIN_MAG_MIP_LINEAR, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK,"                            \
               "addressU = TEXTURE_ADDRESS_BORDER, addressV = TEXTURE_ADDRESS_BORDER, addressW = TEXTURE_ADDRESS_BORDER),"           \
               "StaticSampler(s10, space = 100,"                                                                                     \
               "filter = FILTER_ANISOTROPIC, maxAnisotropy = 8,"                                                                     \
               "addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP)"

#endif

#if defined(__cplusplus)
typedef struct { float m[16]; } float4x4;
typedef uint32_t uint;
#endif

#define INVALID_BINDLESS_INDEX (uint)-1

struct MeshVertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float3 color;
    float2 uv;
};

struct GPUMaterial
{
    float4 baseColor;
    float normalIntensity;
    float occlusionFactor;
    float roughnessFactor;
    float metalnessFactor;
    float emissiveFactor;
    float reflectance;
    uint albedoTextureIndex;
    uint normalTextureIndex;
    uint ormTextureIndex;
    uint emissiveTextureIndex;
    float2 _pad;
};

struct GPUInstance
{
    float4x4 worldMat;
    uint materialBufferOffset;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct GPULight
{
    float3 position;
    float range;
    float3 color;
    float intensity;
};

struct Frame
{
	float4x4 projViewMat;
    float4 cameraPosition;
    float4 sunDirection;
    float4 sunColor;
    uint vertexBufferIndex;
    uint instanceBufferIndex;
    uint materialBufferIndex;
    uint lightBufferIndex;
    uint numLights;
};

#if !defined(__cplusplus)
bool HasValidTexture(uint textureBindlessIndex)
{
    return textureBindlessIndex != INVALID_BINDLESS_INDEX;
}
#endif

#endif // _SHADER_GLOBALS
