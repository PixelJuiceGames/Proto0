#include "Renderer.h"
#include "Scene.h"

#include "DescriptorSets.autogen.h"

// fast_obj

#define FAST_OBJ_IMPLEMENTATION
#include <fast_obj.h>

// MikkTSpace

#include <mikktspace.h>

// The-Forge

#include <Graphics/Interfaces/IGraphics.h>
#include <Utilities/Interfaces/IFileSystem.h>
#include <Utilities/Interfaces/ILog.h>
#include <Utilities/Interfaces/IMemory.h>
#include <Utilities/RingBuffer.h>

extern "C"
{
	__declspec(dllexport) extern const UINT D3D12SDKVersion = 715;
	__declspec(dllexport) extern const char* D3D12SDKPath = "";
}

// Shader Interop

#include <ShaderGlobals.h>

static inline void loadMat4(const ::mat4& matrix, float* output);

const uint32_t k_DataBufferCount = 2;
const uint32_t k_DownsampleSteps = 8;
const uint32_t k_UpsampleSteps = 7;

const uint32_t k_LightsMaxCount = 1024;
const uint32_t k_MaterialsMaxCount = 1024;
const uint32_t k_MeshesMaxCount = 1024;
const uint32_t k_InstancesMaxCount = 1024 * 1024;
const uint32_t k_IndirectDrawCommandsMaxCount = 1024;

enum class Meshes
{
	Plane = 0,
	Cube = 1,
	DamagedHelmet = 2,

	_Count,
};

struct RendererGeometry
{
	MeshVertex* vertices = NULL;
	uint32_t* indices = NULL;

	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
};
struct ScratchGeometryData
{
	RendererGeometry geometry = {};
	uint32_t verticesMaxCount = 256 * 1024;
	uint32_t indicexMaxCount = 1024 * 1024;

	bool isInitialized();
	void initialize();
	void reset();
	void destroy();
};

static ScratchGeometryData k_ScratchGeometryData;

struct RendererState
{
	void* nativeWindowHandle = NULL;

	uint32_t frameIndex = 0;

	::Renderer* renderer = NULL;
	::Queue* graphicsQueue = NULL;
	::GpuCmdRing graphicsCmdRing = {};
	::SwapChain* swapChain = NULL;
	::Semaphore* imageAcquiredSemaphore = NULL;
	// R8G8B8A8_UNORM
	// RGB: Base color, A: unused
	::RenderTarget* gbuffer0 = NULL;
	// R16G16B16A16_FLOAT
	// RGB: World Space Normal, A: unused
	::RenderTarget* gbuffer1 = NULL;
	// R8G8B8A8_UNORM
	// R: Occlusion, G: Roughness, B: Metalness, A: Unused
	::RenderTarget* gbuffer2 = NULL;
	// R16G16B16A16_FLOAT
	// RGB: Emissions, A: unused
	::RenderTarget* gbuffer3 = NULL;
	::RenderTarget* sceneColor = NULL;
	::RenderTarget* depthBuffer = NULL;

	::Buffer* downsampleUniformBuffers[k_DownsampleSteps] = { NULL };
	::Buffer* upsampleUniformBuffers[k_UpsampleSteps] = { NULL };
	::Texture* bloomDownsamples[k_DownsampleSteps] = { NULL };
	::Texture* bloomUpsamples[k_UpsampleSteps] = { NULL };

	RendererGeometry geometry = {};
	::Buffer* meshesBuffer = NULL;
	::Buffer* vertexBuffer = NULL;
	::Buffer* indexBuffer = NULL;

	::Buffer* frameUniformBuffers[k_DataBufferCount] = { NULL };
	::Buffer* instanceBuffers[k_DataBufferCount] = { NULL };
	::Buffer* materialBuffers[k_DataBufferCount] = { NULL };
	::Buffer* lightBuffers[k_DataBufferCount] = { NULL };
	::Buffer* indirectDrawBuffers[k_DataBufferCount] = { NULL };

	GPUMesh* meshes = NULL;
	uint32_t meshCount = 0;

	GPUInstance* instances = NULL;
	uint32_t instanceCount = 0;

	GPUMaterial* materials = NULL;
	uint32_t materialCount = 0;

	GPULight* lights = NULL;
	uint32_t lightsCount = 0;

	IndirectDrawIndexArguments* indirectDrawIndexArgs = NULL;
	uint32_t indirectDrawCommandCount = 0;

	// UberShader
	::Shader* uberShader = NULL;
	::Pipeline* uberPipeline = NULL;
	// NOTE: This is the data of a specific material
	::DescriptorSet* uberPersistentDescriptorSet = NULL;
	::DescriptorSet* uberPerFrameDescriptorSet = NULL;

	// Deferred Sahding
	::Shader* deferredShadingShader = NULL;
	::Pipeline* deferredShadingPipeline = NULL;
	::DescriptorSet* deferredShadingPersistentDescriptorSet = NULL;
	::DescriptorSet* deferredShadingPerFrameDescriptorSet = NULL;

	// Downsampling
	::Shader* downsampleShader = NULL;
	::Pipeline* downsamplePipeline = NULL;
	::DescriptorSet* downsamplePersistentDescriptorSet = NULL;
	::DescriptorSet* downsamplePerDrawDescriptorSet = NULL;

	// Upsampling
	::Shader* upsampleShader = NULL;
	::Pipeline* upsamplePipeline = NULL;
	::DescriptorSet* upsamplePersistentDescriptorSet = NULL;
	::DescriptorSet* upsamplePerDrawDescriptorSet = NULL;

	// Tonemapping
	::Shader* toneMapping = NULL;
	::Pipeline* toneMappingPipeline = NULL;
	::Texture* tonyMcMapfaceLUT = NULL;
	::DescriptorSet* toneMappingPersistentDescriptorSet = NULL;
	::DescriptorSet* toneMappingPerFrameDescriptorSet = NULL;

	::Sampler* linearRepeatSampler = NULL;
	::Sampler* linearClampSampler = NULL;

	// TODO(gmodarelli): Use a pool to store pointers to textures
	::Texture* damagedHelmetAlbedoTexture = NULL;
	::Texture* damagedHelmetNormalTexture = NULL;
	::Texture* damagedHelmetOrmTexture = NULL;
	::Texture* damagedHelmetEmissiveTexture = NULL;
	::Texture* gridAlbedoTexture = NULL;
	::Texture* gridOrmTexture = NULL;
};

static RendererState* g_State = NULL;

bool AddSwapChain();
void RemoveSwapChain();
bool AddRenderTargets();
void RemoveRenderTargets();
void AddShaders();
void RemoveShaders();
void AddDescriptorSets();
void PrepareDescriptorSets();
void RemoveDescriptorSets();
void AddPipelines();
void RemovePipelines();
void AddGeometry();
void RemoveGeometry();
void LoadMesh(struct RendererGeometry* geometry, const char* path, GPUMesh* mesh);

namespace renderer
{
	bool Initialize(void* nativeWindowHandle)
	{
		ASSERT(g_State == NULL);

		// Initialize Memory Allocation System
		{
			if (!::initMemAlloc("Prototype 0"))
			{
				ASSERT(false);
			}
		}
		
		g_State = (RendererState*)tf_malloc(sizeof(RendererState));
		ASSERT(g_State);
		memset(g_State, 0, sizeof(RendererState));

		g_State->nativeWindowHandle = nativeWindowHandle;

		// Initialize The-Forge File System
		{
			FileSystemInitDesc desc = FileSystemInitDesc{};
			desc.pAppName = "Prototype 0";
			if (!::initFileSystem(&desc))
			{
				ASSERT(false);
			}
		}

		// Initialize The-Forge Logging System
		{
			::initLog("Prototype 0", LogLevel::eALL);
		}

		// Initialize The-Forge Renderer
		{
			::RendererDesc desc = RendererDesc{};
			memset((void*)&desc, 0, sizeof(RendererDesc));
			desc.mShaderTarget = ::SHADER_TARGET_6_8;
			::initGPUConfiguration(desc.pExtendedSettings);

			::initRenderer("Prototype 0", &desc, &g_State->renderer);
			if (!g_State->renderer)
			{
				LOGF(eERROR, "Couldn't initialize The-Forge Renderer");
				return false;
			}

			::setupGPUConfigurationPlatformParameters(g_State->renderer, desc.pExtendedSettings);
		}

		// Initialize Graphics Queue
		{
			::QueueDesc queueDesc = {};
			queueDesc.mType = ::QUEUE_TYPE_GRAPHICS;
			queueDesc.mFlag = ::QUEUE_FLAG_INIT_MICROPROFILE;
			::initQueue(g_State->renderer, &queueDesc, &g_State->graphicsQueue);

			::GpuCmdRingDesc cmdRingDesc = {};
			cmdRingDesc.pQueue = g_State->graphicsQueue;
			cmdRingDesc.mPoolCount = k_DataBufferCount;
			cmdRingDesc.mCmdPerPoolCount = 1;
			cmdRingDesc.mAddSyncPrimitives = true;
			::initGpuCmdRing(g_State->renderer, &cmdRingDesc, &g_State->graphicsCmdRing);

			::initSemaphore(g_State->renderer, &g_State->imageAcquiredSemaphore);
		}

		::initResourceLoaderInterface(g_State->renderer);

		::RootSignatureDesc rootDesc = {};
		rootDesc.pGraphicsFileName = "DefaultRootSignature.rs";
		rootDesc.pComputeFileName = "ComputeRootSignature.rs";
		::initRootSignature(g_State->renderer, &rootDesc);

		// Static samplers
		{
			::SamplerDesc samplerDesc = {
				::FILTER_LINEAR,
				::FILTER_LINEAR,
				::MIPMAP_MODE_LINEAR,
				::ADDRESS_MODE_REPEAT,
				::ADDRESS_MODE_REPEAT,
				::ADDRESS_MODE_REPEAT,
			};
			::addSampler(g_State->renderer, &samplerDesc, &g_State->linearRepeatSampler);

			samplerDesc.mAddressU = ::ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerDesc.mAddressV = ::ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerDesc.mAddressW = ::ADDRESS_MODE_CLAMP_TO_EDGE;
			::addSampler(g_State->renderer, &samplerDesc, &g_State->linearClampSampler);
		}

		// Frame uniform buffers
		{
			::BufferLoadDesc ubDesc = {};
			ubDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			ubDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
			ubDesc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
			ubDesc.mDesc.pName = "Frame Uniform Buffer";
			ubDesc.mDesc.mSize = sizeof(Frame);
			ubDesc.pData = NULL;
			for (uint32_t i = 0; i < k_DataBufferCount; ++i)
			{
				ubDesc.ppBuffer = &g_State->frameUniformBuffers[i];
				::addResource(&ubDesc, NULL);
			}
		}

		// Downsample uniform buffers
		{
			::BufferLoadDesc ubDesc = {};
			ubDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			ubDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
			ubDesc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
			ubDesc.mDesc.pName = "Downsample Uniform Buffer";
			ubDesc.mDesc.mSize = sizeof(DownsampleUniform);
			ubDesc.pData = NULL;
			for (uint32_t i = 0; i < k_DownsampleSteps; ++i)
			{
				ubDesc.ppBuffer = &g_State->downsampleUniformBuffers[i];
				::addResource(&ubDesc, NULL);
			}
		}

		// Downsample uniform buffers
		{
			::BufferLoadDesc ubDesc = {};
			ubDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			ubDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
			ubDesc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
			ubDesc.mDesc.pName = "Upsample Uniform Buffer";
			ubDesc.mDesc.mSize = sizeof(UpsampleUniform);
			ubDesc.pData = NULL;
			for (uint32_t i = 0; i < k_UpsampleSteps; ++i)
			{
				ubDesc.ppBuffer = &g_State->upsampleUniformBuffers[i];
				::addResource(&ubDesc, NULL);
			}
		}

		// Load Tony McMapface LUT
		{
			::SyncToken texturesToken = NULL;

			::TextureLoadDesc textureLoadDesc = {};
			memset(&textureLoadDesc, 0, sizeof(::TextureLoadDesc));

			::TextureDesc textureDesc = {};
			memset(&textureDesc, 0, sizeof(::TextureDesc));
			textureDesc.bBindless = false;
			textureLoadDesc.pDesc = &textureDesc;

			textureLoadDesc.pFileName = "Textures/tony_mc_mapface.dds";
			textureLoadDesc.ppTexture = &g_State->tonyMcMapfaceLUT;
			::addResource(&textureLoadDesc, &texturesToken);

			::waitForToken(&texturesToken);
		}

		// Geometry
		AddGeometry();

		// Materials
		{
			// Load PBR Textures
			{
				::SyncToken texturesToken = NULL;

				::TextureLoadDesc textureLoadDesc = {};
				memset(&textureLoadDesc, 0, sizeof(::TextureLoadDesc));

				::TextureDesc textureDesc = {};
				memset(&textureDesc, 0, sizeof(::TextureDesc));
				textureDesc.bBindless = true;
				textureLoadDesc.pDesc = &textureDesc;

				textureLoadDesc.pFileName = "Models/DamagedHelmet_albedo.dds";
				textureLoadDesc.ppTexture = &g_State->damagedHelmetAlbedoTexture;
				::addResource(&textureLoadDesc, &texturesToken);

				textureLoadDesc.pFileName = "Models/DamagedHelmet_normal.dds";
				textureLoadDesc.ppTexture = &g_State->damagedHelmetNormalTexture;
				::addResource(&textureLoadDesc, &texturesToken);

				textureLoadDesc.pFileName = "Models/DamagedHelmet_orm.dds";
				textureLoadDesc.ppTexture = &g_State->damagedHelmetOrmTexture;
				::addResource(&textureLoadDesc, &texturesToken);

				textureLoadDesc.pFileName = "Models/DamagedHelmet_emissive.dds";
				textureLoadDesc.ppTexture = &g_State->damagedHelmetEmissiveTexture;
				::addResource(&textureLoadDesc, &texturesToken);

				textureLoadDesc.pFileName = "Textures/Debug/Grid_albedo.dds";
				textureLoadDesc.ppTexture = &g_State->gridAlbedoTexture;
				::addResource(&textureLoadDesc, &texturesToken);

				textureLoadDesc.pFileName = "Textures/Debug/Grid_orm.dds";
				textureLoadDesc.ppTexture = &g_State->gridOrmTexture;
				::addResource(&textureLoadDesc, &texturesToken);

				::waitForToken(&texturesToken);
			}

			g_State->materials = (GPUMaterial*)tf_malloc(sizeof(GPUMaterial) * k_MaterialsMaxCount);
			ASSERT(g_State->materials);
			memset(g_State->materials, 0, sizeof(GPUMaterial) * k_MaterialsMaxCount);

			GPUMaterial& playerMaterial = g_State->materials[g_State->materialCount++];
			playerMaterial.baseColor = { 0.8f, 0.8f, 0.8f, 1.0f };
			playerMaterial.normalIntensity = 1.0f;
			playerMaterial.occlusionFactor = 1.0f;
			playerMaterial.roughnessFactor = 0.8f;
			playerMaterial.metalnessFactor = 0.0f;
			playerMaterial.emissiveFactor = 0.0f;
			playerMaterial.reflectance = 0.5f;
			playerMaterial.uv0Tiling = { 1.0f, 1.0f };
			playerMaterial.albedoTextureIndex = INVALID_BINDLESS_INDEX;
			playerMaterial.normalTextureIndex = INVALID_BINDLESS_INDEX;
			playerMaterial.ormTextureIndex = INVALID_BINDLESS_INDEX;
			playerMaterial.emissiveTextureIndex = INVALID_BINDLESS_INDEX;

			GPUMaterial& gridMaterial = g_State->materials[g_State->materialCount++];
			gridMaterial.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			gridMaterial.normalIntensity = 1.0f;
			gridMaterial.occlusionFactor = 1.0f;
			gridMaterial.roughnessFactor = 1.0f;
			gridMaterial.metalnessFactor = 0.0f;
			gridMaterial.emissiveFactor = 0.0f;
			gridMaterial.reflectance = 0.5f;
			gridMaterial.uv0Tiling = { 1.0f, 1.0f };
			gridMaterial.albedoTextureIndex = g_State->gridAlbedoTexture->mDx.mDescriptors;
			gridMaterial.normalTextureIndex = INVALID_BINDLESS_INDEX;
			gridMaterial.ormTextureIndex = g_State->gridOrmTexture->mDx.mDescriptors;
			gridMaterial.emissiveTextureIndex = INVALID_BINDLESS_INDEX;

			GPUMaterial& damagedHelmetMaterial = g_State->materials[g_State->materialCount++];
			damagedHelmetMaterial.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			damagedHelmetMaterial.normalIntensity = 1.0f;
			damagedHelmetMaterial.occlusionFactor = 1.0f;
			damagedHelmetMaterial.roughnessFactor = 1.0f;
			damagedHelmetMaterial.metalnessFactor = 0.0f;
			damagedHelmetMaterial.emissiveFactor = 2.0f;
			damagedHelmetMaterial.reflectance = 0.5f;
			damagedHelmetMaterial.uv0Tiling = { 1.0f, 1.0f };
			damagedHelmetMaterial.albedoTextureIndex = g_State->damagedHelmetAlbedoTexture->mDx.mDescriptors;
			damagedHelmetMaterial.normalTextureIndex = g_State->damagedHelmetNormalTexture->mDx.mDescriptors;
			damagedHelmetMaterial.ormTextureIndex = g_State->damagedHelmetOrmTexture->mDx.mDescriptors;
			damagedHelmetMaterial.emissiveTextureIndex = g_State->damagedHelmetEmissiveTexture->mDx.mDescriptors;

			::BufferLoadDesc desc = {};
			desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
			desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
			desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
			desc.mDesc.mSize = sizeof(GPUMaterial) * k_MaterialsMaxCount;
			desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
			desc.mDesc.bBindless = true;
			desc.pData = g_State->materials;
			desc.mDesc.pName = "Materials Buffer";
			for (uint32_t i = 0; i < k_DataBufferCount; ++i)
			{
				desc.ppBuffer = &g_State->materialBuffers[i];
				::addResource(&desc, NULL);
			}
		}

		// Instances
		{
			g_State->instances = (GPUInstance*)tf_malloc(sizeof(GPUInstance) * k_InstancesMaxCount);
			ASSERT(g_State->instances);
			memset(g_State->instances, 0, sizeof(GPUInstance)* k_InstancesMaxCount);
			g_State->instanceCount = 0;

			g_State->indirectDrawIndexArgs = (::IndirectDrawIndexArguments*)tf_malloc(sizeof(::IndirectDrawIndexArguments) * k_IndirectDrawCommandsMaxCount);
			ASSERT(g_State->indirectDrawIndexArgs);
			memset(g_State->indirectDrawIndexArgs, 0, sizeof(::IndirectDrawIndexArguments) * k_IndirectDrawCommandsMaxCount);
			g_State->indirectDrawCommandCount = 0;

			// Instance buffers
			{
				::BufferLoadDesc desc = {};
				desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
				desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
				desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
				desc.mDesc.mSize = sizeof(GPUInstance) * k_InstancesMaxCount;
				desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
				desc.mDesc.bBindless = true;
				desc.mDesc.pName = "Instances Buffer";
				desc.pData = NULL;

				for (uint32_t i = 0; i < k_DataBufferCount; ++i)
				{
					desc.ppBuffer = &g_State->instanceBuffers[i];
					::addResource(&desc, NULL);
				}
			}

			// Indirect draw args buffers
			{
				::BufferLoadDesc desc = {};
				desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_INDIRECT_BUFFER;
				desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
				desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
				desc.mDesc.mSize = sizeof(::IndirectDrawIndexArguments) * k_IndirectDrawCommandsMaxCount;
				desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
				desc.mDesc.mStartState = ::RESOURCE_STATE_INDIRECT_ARGUMENT;
				desc.mDesc.bBindless = false;
				desc.mDesc.pName = "Indirect Draw Buffer";
				desc.pData = NULL;

				for (uint32_t i = 0; i < k_DataBufferCount; ++i)
				{
					desc.ppBuffer = &g_State->indirectDrawBuffers[i];
					::addResource(&desc, NULL);
				}
			}
		}

		// Lights
		{
			g_State->lights = (GPULight*)tf_malloc(sizeof(GPULight) * k_LightsMaxCount);
			ASSERT(g_State->lights);
			memset(g_State->lights, 0, sizeof(GPULight) * k_LightsMaxCount);
			g_State->lightsCount = 0;

			::BufferLoadDesc desc = {};
			desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
			desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
			desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
			desc.mDesc.mSize = sizeof(GPUInstance) * k_LightsMaxCount;
			desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
			desc.mDesc.bBindless = true;
			desc.mDesc.pName = "Lights Buffer";
			desc.pData = NULL;

			for (uint32_t i = 0; i < k_DataBufferCount; ++i)
			{
				desc.ppBuffer = &g_State->lightBuffers[i];
				::addResource(&desc, NULL);
			}
		}

		if (!OnLoad({ ::RELOAD_TYPE_ALL }))
		{
			LOGF(eERROR, "Couldn't load renderer resources");
			return false;
		}

		::waitForAllResourceLoads();

		return true;
	}

	void Exit()
	{
		if (!g_State)
		{
			LOGF(eERROR, "Renderer has not been initialized");
			return;
		}

		tf_free(g_State->materials);
		tf_free(g_State->instances);
		tf_free(g_State->lights);

		OnUnload({ ::RELOAD_TYPE_ALL });

		::exitRootSignature(g_State->renderer);

		RemoveGeometry();

		::removeResource(g_State->gridAlbedoTexture);
		::removeResource(g_State->gridOrmTexture);
		::removeResource(g_State->damagedHelmetAlbedoTexture);
		::removeResource(g_State->damagedHelmetNormalTexture);
		::removeResource(g_State->damagedHelmetOrmTexture);
		::removeResource(g_State->damagedHelmetEmissiveTexture);
		::removeResource(g_State->tonyMcMapfaceLUT);

		::removeSampler(g_State->renderer, g_State->linearRepeatSampler);
		::removeSampler(g_State->renderer, g_State->linearClampSampler);

		for (uint32_t i = 0; i < k_DataBufferCount; ++i)
		{
			::removeResource(g_State->frameUniformBuffers[i]);
			::removeResource(g_State->materialBuffers[i]);
			::removeResource(g_State->instanceBuffers[i]);
			::removeResource(g_State->lightBuffers[i]);
		}

		for (uint32_t i = 0; i < k_DataBufferCount; ++i)
		{
			::removeResource(g_State->indirectDrawBuffers[i]);
		}
		tf_free(g_State->indirectDrawIndexArgs);

		for (uint32_t i = 0; i < k_DownsampleSteps; ++i)
		{
			::removeResource(g_State->downsampleUniformBuffers[i]);
		}

		for (uint32_t i = 0; i < k_UpsampleSteps; ++i)
		{
			::removeResource(g_State->upsampleUniformBuffers[i]);
		}

		::exitGpuCmdRing(g_State->renderer, &g_State->graphicsCmdRing);
		::exitSemaphore(g_State->renderer, g_State->imageAcquiredSemaphore);
		::exitResourceLoaderInterface(g_State->renderer);

		::exitQueue(g_State->renderer, g_State->graphicsQueue);

		::exitRenderer(g_State->renderer);
		::exitGPUConfiguration();
		::exitLog();
		::exitFileSystem();

		tf_free(g_State);
		::exitMemAlloc();
	}

	bool OnLoad(::ReloadDesc reloadDesc)
	{
		ASSERT(g_State);
		ASSERT(g_State->renderer);

		if (reloadDesc.mType & ::RELOAD_TYPE_SHADER)
		{
			AddShaders();
			AddDescriptorSets();
		}

		if (reloadDesc.mType & (::RELOAD_TYPE_RESIZE | ::RELOAD_TYPE_RENDERTARGET))
		{
			if (!AddSwapChain())
			{
				return false;
			}

			if (!AddRenderTargets())
			{
				return false;
			}
		}

		if (reloadDesc.mType & (::RELOAD_TYPE_SHADER | ::RELOAD_TYPE_RENDERTARGET))
		{
			AddPipelines();
		}

		PrepareDescriptorSets();

		return true;
	}

	void OnUnload(::ReloadDesc reloadDesc)
	{
		ASSERT(g_State);
		ASSERT(g_State->renderer);

		::waitQueueIdle(g_State->graphicsQueue);

		if (reloadDesc.mType & (::RELOAD_TYPE_SHADER | ::RELOAD_TYPE_RENDERTARGET))
		{
			RemovePipelines();
		}

		if (reloadDesc.mType & (::RELOAD_TYPE_RESIZE | ::RELOAD_TYPE_RENDERTARGET))
		{
			RemoveSwapChain();
			RemoveRenderTargets();
		}

		if (reloadDesc.mType & ::RELOAD_TYPE_SHADER)
		{
			RemoveDescriptorSets();
			RemoveShaders();
		}
	}

	void LoadScene(const Scene* scene)
	{
		ASSERT(g_State->instances);
		ASSERT(g_State->indirectDrawIndexArgs);

		g_State->instanceCount = 0;
		memset(g_State->instances, 0, sizeof(GPUInstance) * k_InstancesMaxCount);

		g_State->indirectDrawCommandCount = 0;
		memset(g_State->indirectDrawIndexArgs, 0, sizeof(IndirectDrawIndexArguments) * k_IndirectDrawCommandsMaxCount);

		// NOTE: The first instance in the instances buffer stores the player instance
		{
			ASSERT(g_State->instanceCount == 0);
			GPUInstance* playerInstance = &g_State->instances[g_State->instanceCount++];
			const Player* player = &scene->player;
			::mat4 translate = ::mat4::translation({ player->position.x, player->position.y, player->position.z });
			::mat4 scale = ::mat4::scale({ player->scale.x, player->scale.y, player->scale.z });
			loadMat4(translate * scale, &playerInstance->worldMat.m[0]);
			// TODO(gmodarelli): Find a way to associate a mesh/material to an entity in the scene
			uint32_t meshIndex = (uint32_t)Meshes::Cube;
			playerInstance->meshIndex = meshIndex;
			playerInstance->materialBufferIndex = 0;

			// NOTE(gmodarelli): We are currently creating indirect draw arguments on the CPU,
			// but we will move this to the GPU when we start implementing GPU-driven rendering
			ASSERT(g_State->indirectDrawCommandCount == 0);
			const GPUMesh& mesh = g_State->meshes[meshIndex];
			::IndirectDrawIndexArguments* drawIndexArgs = &g_State->indirectDrawIndexArgs[g_State->indirectDrawCommandCount++];
			drawIndexArgs->mIndexCount = mesh.indexCount;
			drawIndexArgs->mStartIndex = mesh.indexOffset;
			drawIndexArgs->mVertexOffset = mesh.vertexOffset;
			drawIndexArgs->mInstanceCount = 1;
			drawIndexArgs->mStartInstance = 0;
		}

		// Load all other instances
		if (scene->entityCount > 0)
		{
			::IndirectDrawIndexArguments currentIndirectDrawArgs = {};

			for (uint32_t i = 0; i < scene->entityCount; ++i)
			{
				const Entity* entity = &scene->entities[i];
				const GPUMesh& mesh = g_State->meshes[entity->meshHandle];
				
				if (i == 0)
				{
					currentIndirectDrawArgs.mIndexCount = mesh.indexCount;
					currentIndirectDrawArgs.mStartIndex = mesh.indexOffset;
					currentIndirectDrawArgs.mVertexOffset = mesh.vertexOffset;
					currentIndirectDrawArgs.mInstanceCount = 1;
					currentIndirectDrawArgs.mStartInstance = g_State->instanceCount;

					GPUInstance* instance = &g_State->instances[g_State->instanceCount++];
					::mat4 translate = ::mat4::translation({ entity->position.x, entity->position.y, entity->position.z });
					::mat4 scale = ::mat4::scale({ entity->scale.x, entity->scale.y, entity->scale.z });
					loadMat4(translate * scale, &instance->worldMat.m[0]);
					instance->meshIndex = entity->meshHandle;
					instance->materialBufferIndex = entity->materialHandle;

					continue;
				}

				if (entity->meshHandle != g_State->instances[g_State->instanceCount - 1].meshIndex)
				{
					::IndirectDrawIndexArguments* indirectDrawArgs = &g_State->indirectDrawIndexArgs[g_State->indirectDrawCommandCount++];
					memcpy(indirectDrawArgs, &currentIndirectDrawArgs, sizeof(::IndirectDrawIndexArguments));

					currentIndirectDrawArgs.mIndexCount = mesh.indexCount;
					currentIndirectDrawArgs.mStartIndex = mesh.indexOffset;
					currentIndirectDrawArgs.mVertexOffset = mesh.vertexOffset;
					currentIndirectDrawArgs.mInstanceCount = 1;
					currentIndirectDrawArgs.mStartInstance = g_State->instanceCount;

					GPUInstance* instance = &g_State->instances[g_State->instanceCount++];
					::mat4 translate = ::mat4::translation({ entity->position.x, entity->position.y, entity->position.z });
					::mat4 scale = ::mat4::scale({ entity->scale.x, entity->scale.y, entity->scale.z });
					loadMat4(translate * scale, &instance->worldMat.m[0]);
					instance->meshIndex = entity->meshHandle;
					instance->materialBufferIndex = entity->materialHandle;
				}
				else
				{
					GPUInstance* instance = &g_State->instances[g_State->instanceCount++];
					::mat4 translate = ::mat4::translation({ entity->position.x, entity->position.y, entity->position.z });
					::mat4 scale = ::mat4::scale({ entity->scale.x, entity->scale.y, entity->scale.z });
					loadMat4(translate * scale, &instance->worldMat.m[0]);
					instance->meshIndex = entity->meshHandle;
					instance->materialBufferIndex = entity->materialHandle;

					currentIndirectDrawArgs.mInstanceCount += 1;
				}
			}

			::IndirectDrawIndexArguments* indirectDrawArgs = &g_State->indirectDrawIndexArgs[g_State->indirectDrawCommandCount++];
			memcpy(indirectDrawArgs, &currentIndirectDrawArgs, sizeof(::IndirectDrawIndexArguments));
		}

		ASSERT(g_State->lights);
		g_State->lightsCount = 0;
		memset(g_State->lights, 0, sizeof(GPULight) * k_LightsMaxCount);

		// NOTE: The first light in the lights buffer stores the player light
		{
			ASSERT(g_State->lightsCount == 0);
			GPULight* playerLight = &g_State->lights[g_State->lightsCount++];
			playerLight->position = scene->playerLight.position;
			playerLight->range = scene->playerLight.range;
			playerLight->color = ::srgbToLinearf3(scene->playerLight.color);
			playerLight->intensity = scene->playerLight.intensity;
		}

		for (uint32_t i = 0; i < scene->lightCount; ++i)
		{
			GPULight* gpuLight = &g_State->lights[g_State->lightsCount++];
			const Light* light = &scene->lights[i];
			gpuLight->position = light->position;
			gpuLight->range = light->range;
			gpuLight->color = ::srgbToLinearf3(light->color);
			gpuLight->intensity = light->intensity;
		}
	}

	void Draw(const Scene* scene)
	{
		RECT rect;
		if (!GetWindowRect((HWND)g_State->nativeWindowHandle, &rect))
		{
			ASSERT(false);
		}
		uint32_t windowWidth = rect.right - rect.left;
		uint32_t windowHeight = rect.bottom - rect.top;

		uint32_t swapChainImageIndex;
		::acquireNextImage(g_State->renderer, g_State->swapChain, g_State->imageAcquiredSemaphore, NULL, &swapChainImageIndex);

		::RenderTarget* swapChainBuffer = g_State->swapChain->ppRenderTargets[swapChainImageIndex];
		::GpuCmdRingElement elem = ::getNextGpuCmdRingElement(&g_State->graphicsCmdRing, true, 1);

		// Stall if CPU is running 2 frames ahead of GPU
		::FenceStatus fenceStatus;
		::getFenceStatus(g_State->renderer, elem.pFence, &fenceStatus);
		if (fenceStatus == ::FENCE_STATUS_INCOMPLETE)
			::waitForFences(g_State->renderer, 1, &elem.pFence);

		::resetCmdPool(g_State->renderer, elem.pCmdPool);

		::Cmd* cmd = elem.pCmds[0];
		::beginCmd(cmd);

		// TODO: Move to a separate function
		// Update GPU data
		{
			// Update Transforms
			// NOTE(gmodarelli): We should split the instance buffer in dynamic and static.
			// For now we're just updating the player transform, since it's the only dynamic instance
			{
				uint32_t playerInstanceIndex = 0;
				uint32_t meshIndex = (uint32_t)Meshes::Cube;
				GPUInstance& instance = g_State->instances[playerInstanceIndex];
				::mat4 translate = ::mat4::translation({
					scene->player.position.x,
					scene->player.position.y,
					scene->player.position.z,
					});
				::mat4 scale = ::mat4::scale({ scene->player.scale.x, scene->player.scale.y, scene->player.scale.z });
				loadMat4(translate* scale, &instance.worldMat.m[0]);
				instance.meshIndex = meshIndex;
				instance.materialBufferIndex = 0;
			}

			// Update player light
			{
				uint32_t playerLightInstance = 0;
				GPULight* playerLight = &g_State->lights[playerLightInstance];
				playerLight->position = scene->playerLight.position;
				playerLight->range = scene->playerLight.range;
				playerLight->color = ::srgbToLinearf3(scene->playerLight.color);
				playerLight->intensity = scene->playerLight.intensity;
			}

			// TODO(gmodarelli): Figure out a way to update only the data that actually
			// changed
			{
				// Upload all instances to the GPU
				{
					::BufferUpdateDesc updateDesc = {};
					updateDesc.pBuffer = g_State->instanceBuffers[g_State->frameIndex];
					updateDesc.mDstOffset = 0;
					updateDesc.mSize = sizeof(GPUInstance) * g_State->instanceCount;
					::beginUpdateResource(&updateDesc);
					memcpy(updateDesc.pMappedData, g_State->instances, sizeof(GPUInstance) * g_State->instanceCount);
					::endUpdateResource(&updateDesc);
				}

				// NOTE(gmodarelli): We are currently creating indirect draw arguments on the CPU,
				// but we will move this to the GPU when we start implementing GPU-driven rendering
				// Upload all indirect draw args to the GPU
				{
					::BufferUpdateDesc updateDesc = {};
					updateDesc.pBuffer = g_State->indirectDrawBuffers[g_State->frameIndex];
					updateDesc.mDstOffset = 0;
					updateDesc.mSize = sizeof(IndirectDrawIndexArguments) * g_State->indirectDrawCommandCount;
					::beginUpdateResource(&updateDesc);
					memcpy(updateDesc.pMappedData, g_State->indirectDrawIndexArgs, sizeof(::IndirectDrawIndexArguments) * g_State->indirectDrawCommandCount);
					::endUpdateResource(&updateDesc);
				}

				// Upload all lights to the GPU
				{
					::BufferUpdateDesc updateDesc = {};
					updateDesc.pBuffer = g_State->lightBuffers[g_State->frameIndex];
					updateDesc.mDstOffset = 0;
					updateDesc.mSize = sizeof(GPULight) * g_State->lightsCount;
					::beginUpdateResource(&updateDesc);
					memcpy(updateDesc.pMappedData, g_State->lights, sizeof(GPULight) * g_State->lightsCount);
					::endUpdateResource(&updateDesc);
				}
			}

			::mat4 projMat = ::mat4::perspectiveRH(1.0471f, windowHeight / (float)windowWidth, 100.0f, 0.01f);
			::mat4 projViewMat = projMat * scene->playerCamera.viewMatrix; 
			::mat4 invProjViewMat = ::inverse(projViewMat);
			Frame frameData = {};
			loadMat4(projViewMat, &frameData.projViewMat.m[0]);
			loadMat4(invProjViewMat, &frameData.invProjViewMat.m[0]);
			frameData.sunColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			frameData.sunDirection = { 0.0f, 0.0f, 0.0f, 0.0f };
			frameData.cameraPosition = { scene->playerCamera.position.x, scene->playerCamera.position.y, scene->playerCamera.position.z, 1.0f };
			frameData.meshBufferIndex = (uint32_t)g_State->meshesBuffer->mDx.mDescriptors;
			frameData.vertexBufferIndex = (uint32_t)g_State->vertexBuffer->mDx.mDescriptors;
			frameData.materialBufferIndex = (uint32_t)g_State->materialBuffers[g_State->frameIndex]->mDx.mDescriptors;
			frameData.instanceBufferIndex = (uint32_t)g_State->instanceBuffers[g_State->frameIndex]->mDx.mDescriptors;
			frameData.lightBufferIndex = (uint32_t)g_State->lightBuffers[g_State->frameIndex]->mDx.mDescriptors;
			frameData.numLights = g_State->lightsCount;

			::BufferUpdateDesc desc = { g_State->frameUniformBuffers[g_State->frameIndex] };
			::beginUpdateResource(&desc);
			memcpy(desc.pMappedData, &frameData, sizeof(frameData));
			::endUpdateResource(&desc);
		}

		// Geometry Pass
		{
			// Resource Barriers
			{
				::RenderTargetBarrier rtBarriers[] = {
					{ g_State->gbuffer0, ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_RENDER_TARGET },
					{ g_State->gbuffer1, ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_RENDER_TARGET },
					{ g_State->gbuffer2, ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_RENDER_TARGET },
					{ g_State->gbuffer3, ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_RENDER_TARGET },
					{ g_State->depthBuffer, ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_DEPTH_WRITE }
				};
				::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(rtBarriers), rtBarriers);
			}

			// Binding Render Targets
			{
				BindRenderTargetsDesc bindRenderTargets = {};
				bindRenderTargets.mRenderTargetCount = 4;
				bindRenderTargets.mRenderTargets[0] = {};
				bindRenderTargets.mRenderTargets[0].pRenderTarget = g_State->gbuffer0;
				bindRenderTargets.mRenderTargets[0].mLoadAction = ::LOAD_ACTION_CLEAR;
				bindRenderTargets.mRenderTargets[1] = {};
				bindRenderTargets.mRenderTargets[1].pRenderTarget = g_State->gbuffer1;
				bindRenderTargets.mRenderTargets[1].mLoadAction = ::LOAD_ACTION_CLEAR;
				bindRenderTargets.mRenderTargets[2] = {};
				bindRenderTargets.mRenderTargets[2].pRenderTarget = g_State->gbuffer2;
				bindRenderTargets.mRenderTargets[2].mLoadAction = ::LOAD_ACTION_CLEAR;
				bindRenderTargets.mRenderTargets[3] = {};
				bindRenderTargets.mRenderTargets[3].pRenderTarget = g_State->gbuffer3;
				bindRenderTargets.mRenderTargets[3].mLoadAction = ::LOAD_ACTION_CLEAR;
				bindRenderTargets.mDepthStencil.mLoadAction = ::LOAD_ACTION_CLEAR;
				bindRenderTargets.mDepthStencil.pDepthStencil = g_State->depthBuffer;
				::cmdBindRenderTargets(cmd, &bindRenderTargets);
			}

			::cmdSetViewport(cmd, 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f);
			::cmdSetScissor(cmd, 0, 0, (uint32_t)windowWidth, (uint32_t)windowHeight);


			// Render meshes
			{
				::cmdBindPipeline(cmd, g_State->uberPipeline);
				::cmdBindDescriptorSet(cmd, 0, g_State->uberPersistentDescriptorSet);
				::cmdBindDescriptorSet(cmd, g_State->frameIndex, g_State->uberPerFrameDescriptorSet);
				::cmdBindIndexBuffer(cmd, g_State->indexBuffer, ::INDEX_TYPE_UINT32, 0);

				::cmdExecuteIndirect(cmd, ::INDIRECT_DRAW_INDEX, g_State->indirectDrawCommandCount, g_State->indirectDrawBuffers[g_State->frameIndex], 0, NULL, 0);
			}

			::cmdBindRenderTargets(cmd, NULL);
		}

		// Deferred Shading
		{
			// Resource Barriers
			{
				::RenderTargetBarrier rtBarriers[] = {
					{ g_State->gbuffer0, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_SHADER_RESOURCE },
					{ g_State->gbuffer1, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_SHADER_RESOURCE },
					{ g_State->gbuffer2, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_SHADER_RESOURCE },
					{ g_State->gbuffer3, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_SHADER_RESOURCE },
					{ g_State->depthBuffer, ::RESOURCE_STATE_DEPTH_WRITE, ::RESOURCE_STATE_SHADER_RESOURCE },
					{ g_State->sceneColor, ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_RENDER_TARGET },
				};
				::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(rtBarriers), rtBarriers);
			}

			// Binding Render Targets
			{
				BindRenderTargetsDesc bindRenderTargets = {};
				bindRenderTargets.mRenderTargetCount = 1;
				bindRenderTargets.mRenderTargets[0] = {};
				bindRenderTargets.mRenderTargets[0].pRenderTarget = g_State->sceneColor;
				bindRenderTargets.mRenderTargets[0].mLoadAction = ::LOAD_ACTION_CLEAR;
				::cmdBindRenderTargets(cmd, &bindRenderTargets);
			}

			::cmdSetViewport(cmd, 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f);
			::cmdSetScissor(cmd, 0, 0, (uint32_t)windowWidth, (uint32_t)windowHeight);

			::cmdBindPipeline(cmd, g_State->deferredShadingPipeline);
			::cmdBindDescriptorSet(cmd, 0, g_State->deferredShadingPersistentDescriptorSet);
			::cmdBindDescriptorSet(cmd, g_State->frameIndex, g_State->deferredShadingPerFrameDescriptorSet);
			::cmdDraw(cmd, 3, 0);
		}

		// Bloom
		{
			// Downsample
			for (uint32_t i = 0; i < k_DownsampleSteps; ++i)
			{
				// NOTE(gmodarelli): This could be a simple push constant
				DownsampleUniform uniform = {};
				if (i == 0)
				{
					uniform.inputSize = { g_State->sceneColor->mWidth, g_State->sceneColor->mHeight };

					// Resource Barriers
					{
						::RenderTargetBarrier rtBarriers[] = {
							{ g_State->sceneColor, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_SHADER_RESOURCE }
						};
						::TextureBarrier tBarriers[] = {
							{ g_State->bloomDownsamples[i], ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_UNORDERED_ACCESS }
						};
						::cmdResourceBarrier(cmd, 0, NULL, TF_ARRAY_COUNT(tBarriers), tBarriers, TF_ARRAY_COUNT(rtBarriers), rtBarriers);
					}
				}
				else
				{
					uniform.inputSize = { g_State->bloomDownsamples[i - 1]->mWidth, g_State->bloomDownsamples[i - 1]->mHeight };

					// Resource Barriers
					{
						::TextureBarrier tBarriers[] = {
							{ g_State->bloomDownsamples[i - 1], ::RESOURCE_STATE_UNORDERED_ACCESS, ::RESOURCE_STATE_SHADER_RESOURCE },
							{ g_State->bloomDownsamples[i], ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_UNORDERED_ACCESS }
						};
						::cmdResourceBarrier(cmd, 0, NULL, TF_ARRAY_COUNT(tBarriers), tBarriers, 0, NULL);
					}
				}

				::BufferUpdateDesc desc = { g_State->downsampleUniformBuffers[i] };
				::beginUpdateResource(&desc);
				memcpy(desc.pMappedData, &uniform, sizeof(uniform));
				::endUpdateResource(&desc);

				::cmdBindPipeline(cmd, g_State->downsamplePipeline);
				::cmdBindDescriptorSet(cmd, 0, g_State->downsamplePersistentDescriptorSet);
				::cmdBindDescriptorSet(cmd, i, g_State->downsamplePerDrawDescriptorSet);
				::cmdDispatch(cmd, (g_State->bloomDownsamples[i]->mWidth / 8) + 1, (g_State->bloomDownsamples[i]->mHeight / 8) + 1, 1);
			}

			// Upsample
			for (uint32_t i = 0; i < k_UpsampleSteps; ++i)
			{
				UpsampleUniform uniform = {};
				uniform.radius = 0.75f;

				if (i == 0)
				{
					uniform.inputSize = { g_State->bloomDownsamples[k_DownsampleSteps - 1]->mWidth, g_State->bloomDownsamples[k_DownsampleSteps - 1]->mHeight };

					// Resource Barriers
					{
						::TextureBarrier tBarriers[] = {
							{ g_State->bloomDownsamples[k_DownsampleSteps - 1], ::RESOURCE_STATE_UNORDERED_ACCESS, ::RESOURCE_STATE_SHADER_RESOURCE },
							{ g_State->bloomUpsamples[i], ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_UNORDERED_ACCESS }
						};
						::cmdResourceBarrier(cmd, 0, NULL, TF_ARRAY_COUNT(tBarriers), tBarriers, 0, NULL);
					}
				}
				else
				{
					uniform.inputSize = { g_State->bloomUpsamples[i - 1]->mWidth, g_State->bloomUpsamples[i - 1]->mHeight };

					// Resource Barriers
					{
						::TextureBarrier tBarriers[] = {
							{ g_State->bloomUpsamples[i - 1], ::RESOURCE_STATE_UNORDERED_ACCESS, ::RESOURCE_STATE_SHADER_RESOURCE },
							{ g_State->bloomUpsamples[i], ::RESOURCE_STATE_SHADER_RESOURCE, ::RESOURCE_STATE_UNORDERED_ACCESS }
						};
						::cmdResourceBarrier(cmd, 0, NULL, TF_ARRAY_COUNT(tBarriers), tBarriers, 0, NULL);
					}
				}

				::BufferUpdateDesc desc = { g_State->upsampleUniformBuffers[i] };
				::beginUpdateResource(&desc);
				memcpy(desc.pMappedData, &uniform, sizeof(uniform));
				::endUpdateResource(&desc);

				::cmdBindPipeline(cmd, g_State->upsamplePipeline);
				::cmdBindDescriptorSet(cmd, 0, g_State->upsamplePersistentDescriptorSet);
				::cmdBindDescriptorSet(cmd, i, g_State->upsamplePerDrawDescriptorSet);
				::cmdDispatch(cmd, (g_State->bloomUpsamples[i]->mWidth / 8) + 1, (g_State->bloomUpsamples[i]->mHeight / 8) + 1, 1);
			}

			// Resource Barriers
			{
				::TextureBarrier tBarriers[] = {
					{ g_State->bloomUpsamples[k_UpsampleSteps - 1], ::RESOURCE_STATE_UNORDERED_ACCESS, ::RESOURCE_STATE_SHADER_RESOURCE }
				};
				::cmdResourceBarrier(cmd, 0, NULL, TF_ARRAY_COUNT(tBarriers), tBarriers, 0, NULL);
			}
		}

		// Tone Mapping Pass
		{
			// Resource Barriers
			{
				::RenderTargetBarrier rtBarriers[] = {
					{ swapChainBuffer, ::RESOURCE_STATE_PRESENT, ::RESOURCE_STATE_RENDER_TARGET },
				};
				::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(rtBarriers), rtBarriers);
			}

			// Binding Render Targets
			{
				BindRenderTargetsDesc bindRenderTargets = {};
				bindRenderTargets.mRenderTargetCount = 1;
				bindRenderTargets.mRenderTargets[0] = {};
				bindRenderTargets.mRenderTargets[0].pRenderTarget = swapChainBuffer;
				bindRenderTargets.mRenderTargets[0].mLoadAction = ::LOAD_ACTION_CLEAR;
				::cmdBindRenderTargets(cmd, &bindRenderTargets);
			}

			::cmdSetViewport(cmd, 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f);
			::cmdSetScissor(cmd, 0, 0, (uint32_t)windowWidth, (uint32_t)windowHeight);

			::cmdBindPipeline(cmd, g_State->toneMappingPipeline);
			::cmdBindDescriptorSet(cmd, 0, g_State->toneMappingPersistentDescriptorSet);
			::cmdBindDescriptorSet(cmd, g_State->frameIndex, g_State->toneMappingPerFrameDescriptorSet);
			::cmdDraw(cmd, 3, 0);

			// Resource Barriers
			{
				::RenderTargetBarrier rtBarriers[] = {
					{ swapChainBuffer, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_PRESENT },
				};
				::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(rtBarriers), rtBarriers);
			}

			::cmdBindRenderTargets(cmd, NULL);
		}

		::endCmd(cmd);

		::FlushResourceUpdateDesc flushUpdateDesc = {};
		flushUpdateDesc.mNodeIndex = 0;
		::flushResourceUpdates(&flushUpdateDesc);
		::Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, g_State->imageAcquiredSemaphore };

		::QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &elem.pSemaphore;
		submitDesc.ppWaitSemaphores = waitSemaphores;
		submitDesc.pSignalFence = elem.pFence;
		::queueSubmit(g_State->graphicsQueue, &submitDesc);

		::QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = (uint8_t)swapChainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.pSwapChain = g_State->swapChain;
		presentDesc.ppWaitSemaphores = &elem.pSemaphore;
		presentDesc.mSubmitDone = true;

		::queuePresent(g_State->graphicsQueue, &presentDesc);

		g_State->frameIndex = (g_State->frameIndex + 1) % k_DataBufferCount;
	}
}

bool AddSwapChain()
{
	::WindowHandle windowHandle = { ::WINDOW_HANDLE_TYPE_WIN32, (HWND)g_State->nativeWindowHandle };

	RECT rect;
	if (!GetWindowRect((HWND)g_State->nativeWindowHandle, &rect))
	{
		ASSERT(false);
	}
	uint32_t windowWidth = rect.right - rect.left;
	uint32_t windowHeight = rect.bottom - rect.top;

	::SwapChainDesc desc = {};
	desc.mWindowHandle = windowHandle;
	desc.mPresentQueueCount = 1;
	desc.ppPresentQueues = &g_State->graphicsQueue;
	desc.mWidth = windowWidth;
	desc.mHeight = windowHeight;
	desc.mImageCount = ::getRecommendedSwapchainImageCount(g_State->renderer, &windowHandle);
	desc.mColorFormat = ::getSupportedSwapchainFormat(g_State->renderer, &desc, ::COLOR_SPACE_SDR_SRGB);
	desc.mColorSpace = ::COLOR_SPACE_SDR_SRGB;
	desc.mEnableVsync = true;
	desc.mFlags = ::SWAP_CHAIN_CREATION_FLAG_NONE;
	::addSwapChain(g_State->renderer, &desc, &g_State->swapChain);

	return g_State->swapChain != NULL;
}

void RemoveSwapChain()
{
	::removeSwapChain(g_State->renderer, g_State->swapChain);
}

bool AddRenderTargets()
{
	RECT rect;
	if (!GetWindowRect((HWND)g_State->nativeWindowHandle, &rect))
	{
		ASSERT(false);
	}
	uint32_t windowWidth = rect.right - rect.left;
	uint32_t windowHeight = rect.bottom - rect.top;

	// Add Depth Buffer
	{
		::RenderTargetDesc desc = {};
		desc.mWidth = windowWidth;
		desc.mHeight = windowHeight;
		desc.mDepth = 1;
		desc.mArraySize = 1;
		desc.mClearValue.depth = 0.0f;
		desc.mClearValue.stencil = 0;
		desc.mFormat = ::TinyImageFormat_D32_SFLOAT;
		desc.mStartState = ::RESOURCE_STATE_SHADER_RESOURCE;
		desc.mSampleCount = ::SAMPLE_COUNT_1;
		desc.mSampleQuality = 0;
		desc.mFlags = ::TEXTURE_CREATION_FLAG_ON_TILE;
		::addRenderTarget(g_State->renderer, &desc, &g_State->depthBuffer);

		if (!g_State->depthBuffer)
		{
			LOGF(eERROR, "Failed to create depth buffer");
			return false;
		}
	}

	// Add Scene Color
	{
		::RenderTargetDesc desc = {};
		desc.mWidth = (uint32_t)windowWidth;
		desc.mHeight = (uint32_t)windowHeight;
		desc.mDepth = 1;
		desc.mArraySize = 1;
		desc.mClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
		desc.mFormat = ::TinyImageFormat_R16G16B16A16_SFLOAT;
		desc.mStartState = ::RESOURCE_STATE_SHADER_RESOURCE;
		desc.mSampleCount = ::SAMPLE_COUNT_1;
		desc.mSampleQuality = 0;
		desc.mFlags = ::TEXTURE_CREATION_FLAG_ON_TILE;
		::addRenderTarget(g_State->renderer, &desc, &g_State->sceneColor);

		if (!g_State->sceneColor)
		{
			LOGF(eERROR, "Failed to create the scene color buffer");
			return false;
		}
	}

	// GBuffer
	{
		::RenderTargetDesc desc = {};
		desc.mWidth = (uint32_t)windowWidth;
		desc.mHeight = (uint32_t)windowHeight;
		desc.mDepth = 1;
		desc.mArraySize = 1;
		desc.mClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
		desc.mStartState = ::RESOURCE_STATE_SHADER_RESOURCE;
		desc.mSampleCount = ::SAMPLE_COUNT_1;
		desc.mSampleQuality = 0;
		desc.mFlags = ::TEXTURE_CREATION_FLAG_ON_TILE;

		// GBuffer 0
		desc.mFormat = ::TinyImageFormat_R8G8B8A8_SRGB;
		desc.pName = "GBuffer 0";
		::addRenderTarget(g_State->renderer, &desc, &g_State->gbuffer0);

		if (!g_State->gbuffer0)
		{
			LOGF(eERROR, "Failed to create the gbuffer 0");
			return false;
		}

		// GBuffer 1
		desc.mFormat = ::TinyImageFormat_R16G16B16A16_SFLOAT;
		desc.pName = "GBuffer 1";
		::addRenderTarget(g_State->renderer, &desc, &g_State->gbuffer1);

		if (!g_State->gbuffer1)
		{
			LOGF(eERROR, "Failed to create the gbuffer 1");
			return false;
		}

		// GBuffer 2
		desc.mFormat = ::TinyImageFormat_R8G8B8A8_UNORM;
		desc.pName = "GBuffer 2";
		::addRenderTarget(g_State->renderer, &desc, &g_State->gbuffer2);

		if (!g_State->gbuffer2)
		{
			LOGF(eERROR, "Failed to create the gbuffer 2");
			return false;
		}

		// GBuffer 3
		desc.mFormat = ::TinyImageFormat_R16G16B16A16_SFLOAT;
		desc.pName = "GBuffer 3";
		::addRenderTarget(g_State->renderer, &desc, &g_State->gbuffer3);

		if (!g_State->gbuffer3)
		{
			LOGF(eERROR, "Failed to create the gbuffer 3");
			return false;
		}
	}

	// Add downsample textures
	{
		::TextureDesc desc = {};
		desc.mDepth = 1;
		desc.mArraySize = 1;
		desc.mMipLevels = 1;
		desc.mFormat = ::TinyImageFormat_R16G16B16A16_SFLOAT;
		desc.mStartState = ::RESOURCE_STATE_SHADER_RESOURCE;
		desc.mDescriptors = ::DESCRIPTOR_TYPE_TEXTURE | ::DESCRIPTOR_TYPE_RW_TEXTURE;
		desc.mSampleCount = ::SAMPLE_COUNT_1;
		desc.mSampleQuality = 0;
		desc.bBindless = false;
		::TextureLoadDesc loadDesc = {};
		loadDesc.pDesc = &desc;

		::SyncToken texturesToken = NULL;
		for (uint32_t i = 0; i < k_DownsampleSteps; i++)
		{
			desc.mWidth = (uint32_t)windowWidth >> (i + 1);
			desc.mHeight = (uint32_t)windowHeight >> (i + 1);
			loadDesc.ppTexture = &g_State->bloomDownsamples[i];
			::addResource(&loadDesc, &texturesToken);
		}

		::waitForToken(&texturesToken);
	}

	// Add upsample textures
	{
		::TextureDesc desc = {};
		desc.mDepth = 1;
		desc.mArraySize = 1;
		desc.mMipLevels = 1;
		desc.mFormat = ::TinyImageFormat_R16G16B16A16_SFLOAT;
		desc.mStartState = ::RESOURCE_STATE_SHADER_RESOURCE;
		desc.mDescriptors = ::DESCRIPTOR_TYPE_TEXTURE | ::DESCRIPTOR_TYPE_RW_TEXTURE;
		desc.mSampleCount = ::SAMPLE_COUNT_1;
		desc.mSampleQuality = 0;
		desc.bBindless = false;
		::TextureLoadDesc loadDesc = {};
		loadDesc.pDesc = &desc;

		::SyncToken texturesToken = NULL;
		for (uint32_t i = 0; i < k_UpsampleSteps; ++i)
		{
			desc.mWidth = (uint32_t)windowWidth >> (k_UpsampleSteps - i);
			desc.mHeight = (uint32_t)windowHeight >> (k_UpsampleSteps - i);
			loadDesc.ppTexture = &g_State->bloomUpsamples[i];
			::addResource(&loadDesc, &texturesToken);
		}

		::waitForToken(&texturesToken);
	}

	return true;
}

void RemoveRenderTargets()
{
	::removeRenderTarget(g_State->renderer, g_State->depthBuffer);
	::removeRenderTarget(g_State->renderer, g_State->sceneColor);
	::removeRenderTarget(g_State->renderer, g_State->gbuffer0);
	::removeRenderTarget(g_State->renderer, g_State->gbuffer1);
	::removeRenderTarget(g_State->renderer, g_State->gbuffer2);
	::removeRenderTarget(g_State->renderer, g_State->gbuffer3);

	for (uint32_t i = 0; i < k_DownsampleSteps; i++)
	{
		::removeResource(g_State->bloomDownsamples[i]);
	}

	for (uint32_t i = 0; i < k_UpsampleSteps; i++)
	{
		::removeResource(g_State->bloomUpsamples[i]);
	}
}

void AddShaders()
{
	// Uber Shader
	{
		ShaderLoadDesc uberShader = {};
		uberShader.mVert.pFileName = "Uber.vert";
		uberShader.mFrag.pFileName = "Uber.pixel";
		::addShader(g_State->renderer, &uberShader, &g_State->uberShader);
	}

	// TODO: Switch to a compute shader/pipeline
	// Deferred Shading
	{
		ShaderLoadDesc uberShader = {};
		uberShader.mVert.pFileName = "FullscreenTriangle.vert";
		uberShader.mFrag.pFileName = "DeferredShading.pixel";
		::addShader(g_State->renderer, &uberShader, &g_State->deferredShadingShader);
	}

	// Downsample Shader
	{
		ShaderLoadDesc downsampleShader = {};
		downsampleShader.mComp.pFileName = "Downsample.comp";
		::addShader(g_State->renderer, &downsampleShader, &g_State->downsampleShader);
	}

	// Upsample Shader
	{
		ShaderLoadDesc upsampleShader = {};
		upsampleShader.mComp.pFileName = "Upsample.comp";
		::addShader(g_State->renderer, &upsampleShader, &g_State->upsampleShader);
	}

	// Tone mapping
	{
		ShaderLoadDesc uberShader = {};
		uberShader.mVert.pFileName = "FullscreenTriangle.vert";
		uberShader.mFrag.pFileName = "Tonemap.pixel";
		::addShader(g_State->renderer, &uberShader, &g_State->toneMapping);
	}
}

void RemoveShaders()
{
	::removeShader(g_State->renderer, g_State->uberShader);
	::removeShader(g_State->renderer, g_State->deferredShadingShader);
	::removeShader(g_State->renderer, g_State->downsampleShader);
	::removeShader(g_State->renderer, g_State->upsampleShader);
	::removeShader(g_State->renderer, g_State->toneMapping);
}

void AddDescriptorSets()
{
	// Uber Shader Descriptor Sets
	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_Persistent_SAMPLER;
		desc.mMaxSets = 1;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_UberShaderData::PersistentPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->uberPersistentDescriptorSet);
	}

	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_PerFrame;
		desc.mMaxSets = k_DataBufferCount;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_UberShaderData::PerFramePtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->uberPerFrameDescriptorSet);
	}

	// Deferred Shading Shader Descriptor Sets
	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_Persistent_SAMPLER;
		desc.mMaxSets = 1;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_DeferredShadingShaderData::PersistentPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->deferredShadingPersistentDescriptorSet);
	}

	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_PerFrame;
		desc.mMaxSets = k_DataBufferCount;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 6;
		desc.pDescriptors = SRT_DeferredShadingShaderData::PerFramePtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->deferredShadingPerFrameDescriptorSet);
	}

	// Downsample Descriptor Sets
	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_Persistent_SAMPLER;
		desc.mMaxSets = 1;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_DownsampleData::PersistentPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->downsamplePersistentDescriptorSet);
	}

	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_PerDraw;
		desc.mMaxSets = k_DownsampleSteps;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 3;
		desc.pDescriptors = SRT_DownsampleData::PerDrawPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->downsamplePerDrawDescriptorSet);
	}

	// Upsample Descriptor Sets
	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_Persistent_SAMPLER;
		desc.mMaxSets = 1;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_UpsampleData::PersistentPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->upsamplePersistentDescriptorSet);
	}

	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_PerDraw;
		desc.mMaxSets = k_UpsampleSteps;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 4;
		desc.pDescriptors = SRT_UpsampleData::PerDrawPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->upsamplePerDrawDescriptorSet);
	}

	// Tone Mapping Descriptor Sets
	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_Persistent_SAMPLER;
		desc.mMaxSets = 1;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_ToneMappingData::PersistentPtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->toneMappingPersistentDescriptorSet);
	}

	{
		::DescriptorSetDesc desc = {};
		desc.mIndex = ROOT_PARAM_PerFrame;
		desc.mMaxSets = k_DataBufferCount;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 3;
		desc.pDescriptors = SRT_ToneMappingData::PerFramePtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->toneMappingPerFrameDescriptorSet);
	}
}

void PrepareDescriptorSets()
{
	// Uber Shader Descriptor Sets
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_UberShaderData::Persistent, gLinearRepeatSampler)) / sizeof(::Descriptor);
		uParams[0].ppSamplers = &g_State->linearRepeatSampler;
		updateDescriptorSet(g_State->renderer, 0, g_State->uberPersistentDescriptorSet, 1, uParams);
	}

	for (uint32_t i = 0; i < k_DataBufferCount; ++i)
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_UberShaderData::PerFrame, CB0)) / sizeof(::Descriptor);
		uParams[0].ppBuffers = &g_State->frameUniformBuffers[i];
		updateDescriptorSet(g_State->renderer, i, g_State->uberPerFrameDescriptorSet, 1, uParams);
	}

	// Deferred Shading Shader Descriptor Sets
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_DeferredShadingShaderData::Persistent, gLinearClampSampler)) / sizeof(::Descriptor);
		uParams[0].ppSamplers = &g_State->linearClampSampler;
		updateDescriptorSet(g_State->renderer, 0, g_State->deferredShadingPersistentDescriptorSet, 1, uParams);
	}

	for (uint32_t i = 0; i < k_DataBufferCount; ++i)
	{
		DescriptorData uParams[6] = {};
		uParams[0].mIndex = (offsetof(SRT_DeferredShadingShaderData::PerFrame, CB0)) / sizeof(::Descriptor);
		uParams[0].ppBuffers = &g_State->frameUniformBuffers[i];
		uParams[1].mIndex = (offsetof(SRT_DeferredShadingShaderData::PerFrame, gGBuffer0)) / sizeof(::Descriptor);
		uParams[1].ppTextures = &g_State->gbuffer0->pTexture;
		uParams[2].mIndex = (offsetof(SRT_DeferredShadingShaderData::PerFrame, gGBuffer1)) / sizeof(::Descriptor);
		uParams[2].ppTextures = &g_State->gbuffer1->pTexture;
		uParams[3].mIndex = (offsetof(SRT_DeferredShadingShaderData::PerFrame, gGBuffer2)) / sizeof(::Descriptor);
		uParams[3].ppTextures = &g_State->gbuffer2->pTexture;
		uParams[4].mIndex = (offsetof(SRT_DeferredShadingShaderData::PerFrame, gGBuffer3)) / sizeof(::Descriptor);
		uParams[4].ppTextures = &g_State->gbuffer3->pTexture;
		uParams[5].mIndex = (offsetof(SRT_DeferredShadingShaderData::PerFrame, gDepthBuffer)) / sizeof(::Descriptor);
		uParams[5].ppTextures = &g_State->depthBuffer->pTexture;
		updateDescriptorSet(g_State->renderer, i, g_State->deferredShadingPerFrameDescriptorSet, TF_ARRAY_COUNT(uParams), uParams);
	}

	// Downsample Descriptor Sets
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_DownsampleData::Persistent, gLinearClampSampler)) / sizeof(::Descriptor);
		uParams[0].ppSamplers = &g_State->linearClampSampler;
		updateDescriptorSet(g_State->renderer, 0, g_State->downsamplePersistentDescriptorSet, 1, uParams);
	}

	for (uint32_t i = 0; i < k_DownsampleSteps; ++i)
	{
		DescriptorData uParams[3] = {};
		uParams[0].mIndex = (offsetof(SRT_DownsampleData::PerDraw, CB0)) / sizeof(::Descriptor);
		uParams[0].ppBuffers = &g_State->downsampleUniformBuffers[i];
		uParams[1].mIndex = (offsetof(SRT_DownsampleData::PerDraw, gSourceTexture)) / sizeof(::Descriptor);
		if (i == 0) {
			uParams[1].ppTextures = &g_State->sceneColor->pTexture;
		}
		else {
			uParams[1].ppTextures = &g_State->bloomDownsamples[i - 1];
		}
		uParams[2].mIndex = (offsetof(SRT_DownsampleData::PerDraw, gDestinationTexture)) / sizeof(::Descriptor);
		uParams[2].ppTextures = &g_State->bloomDownsamples[i];
		updateDescriptorSet(g_State->renderer, i, g_State->downsamplePerDrawDescriptorSet, 3, uParams);
	}

	// Upsample Descriptor Sets
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_UpsampleData::Persistent, gLinearClampSampler)) / sizeof(::Descriptor);
		uParams[0].ppSamplers = &g_State->linearClampSampler;
		updateDescriptorSet(g_State->renderer, 0, g_State->upsamplePersistentDescriptorSet, 1, uParams);
	}

	for (uint32_t i = 0; i < k_UpsampleSteps; ++i)
	{
		DescriptorData uParams[4] = {};
		uParams[0].mIndex = (offsetof(SRT_UpsampleData::PerDraw, CB0)) / sizeof(::Descriptor);
		uParams[0].ppBuffers = &g_State->upsampleUniformBuffers[i];
		uParams[1].mIndex = (offsetof(SRT_UpsampleData::PerDraw, gSourceTexture)) / sizeof(::Descriptor);
		uParams[1].ppTextures = &g_State->bloomDownsamples[k_UpsampleSteps - (i + 1)];
		uParams[2].mIndex = (offsetof(SRT_UpsampleData::PerDraw, gPreviousTexture)) / sizeof(::Descriptor);
		if (i == 0)
		{
			uParams[2].ppTextures = &g_State->bloomDownsamples[k_DownsampleSteps - 1];
		}
		else
		{
			uParams[2].ppTextures = &g_State->bloomUpsamples[i - 1];
		}
		uParams[3].mIndex = (offsetof(SRT_UpsampleData::PerDraw, gDestinationTexture)) / sizeof(::Descriptor);
		uParams[3].ppTextures = &g_State->bloomUpsamples[i];
		updateDescriptorSet(g_State->renderer, i, g_State->upsamplePerDrawDescriptorSet, 4, uParams);
	}

	// Tone Mapping Descriptor Sets
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_ToneMappingData::Persistent, gLinearClampSampler)) / sizeof(::Descriptor);
		uParams[0].ppSamplers = &g_State->linearClampSampler;
		updateDescriptorSet(g_State->renderer, 0, g_State->toneMappingPersistentDescriptorSet, 1, uParams);
	}

	for (uint32_t i = 0; i < k_DataBufferCount; ++i)
	{
		DescriptorData uParams[3] = {};
		uParams[0].mIndex = (offsetof(SRT_ToneMappingData::PerFrame, gSceneColor)) / sizeof(::Descriptor);
		uParams[0].ppTextures = &g_State->sceneColor->pTexture;
		uParams[1].mIndex = (offsetof(SRT_ToneMappingData::PerFrame, gTonyMcMapfaceLut)) / sizeof(::Descriptor);
		uParams[1].ppTextures = &g_State->tonyMcMapfaceLUT;
		uParams[2].mIndex = (offsetof(SRT_ToneMappingData::PerFrame, gBloomBuffer)) / sizeof(::Descriptor);
		uParams[2].ppTextures = &g_State->bloomUpsamples[k_UpsampleSteps - 1];
		updateDescriptorSet(g_State->renderer, i, g_State->toneMappingPerFrameDescriptorSet, 3, uParams);
	}
}

void RemoveDescriptorSets()
{
	::removeDescriptorSet(g_State->renderer, g_State->uberPersistentDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->uberPerFrameDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->deferredShadingPersistentDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->deferredShadingPerFrameDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->downsamplePersistentDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->downsamplePerDrawDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->upsamplePersistentDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->upsamplePerDrawDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->toneMappingPersistentDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->toneMappingPerFrameDescriptorSet);
}

void AddPipelines()
{
	// Uber Shader Pipeline
	{
		::RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = ::CULL_MODE_BACK;
		rasterizerStateDesc.mFrontFace = ::FRONT_FACE_CCW;

		::DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		depthStateDesc.mDepthFunc = ::CMP_GEQUAL;

		::TinyImageFormat colorFormats[] = {
			g_State->gbuffer0->mFormat,
			g_State->gbuffer1->mFormat,
			g_State->gbuffer2->mFormat,
			g_State->gbuffer3->mFormat,
		};

		::PipelineDesc desc = {};
		desc.mType = ::PIPELINE_TYPE_GRAPHICS;
		::GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = ::PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = TF_ARRAY_COUNT(colorFormats);
		pipelineSettings.pColorFormats = colorFormats;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.mSampleCount = ::SAMPLE_COUNT_1;
		pipelineSettings.mSampleQuality = 0;
		pipelineSettings.mDepthStencilFormat = g_State->depthBuffer->mFormat;
		pipelineSettings.pShaderProgram = g_State->uberShader;
		pipelineSettings.pVertexLayout = NULL;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.mVRFoveatedRendering = false;
		::addPipeline(g_State->renderer, &desc, &g_State->uberPipeline);
	}

	// Deferred Shading Shader Pipeline
	{
		::RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = ::CULL_MODE_NONE;

		::DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = false;
		depthStateDesc.mDepthWrite = false;

		::PipelineDesc desc = {};
		desc.mType = ::PIPELINE_TYPE_GRAPHICS;
		::GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = ::PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &g_State->sceneColor->mFormat;
		pipelineSettings.mSampleCount = g_State->sceneColor->mSampleCount;
		pipelineSettings.mSampleQuality = g_State->sceneColor->mSampleQuality;
		pipelineSettings.pShaderProgram = g_State->deferredShadingShader;
		pipelineSettings.pVertexLayout = NULL;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.mVRFoveatedRendering = false;
		::addPipeline(g_State->renderer, &desc, &g_State->deferredShadingPipeline);
	}

	// Downsample Pipeline
	{
		::PipelineDesc desc = {};
		desc.mType = ::PIPELINE_TYPE_COMPUTE;

		::ComputePipelineDesc& pipelineSettings = desc.mComputeDesc;
		pipelineSettings.pShaderProgram = g_State->downsampleShader;
		::addPipeline(g_State->renderer, &desc, &g_State->downsamplePipeline);
	}

	// Upsample Pipeline
	{
		::PipelineDesc desc = {};
		desc.mType = ::PIPELINE_TYPE_COMPUTE;

		::ComputePipelineDesc& pipelineSettings = desc.mComputeDesc;
		pipelineSettings.pShaderProgram = g_State->upsampleShader;
		::addPipeline(g_State->renderer, &desc, &g_State->upsamplePipeline);
	}

	// Tone Mapping Pipeline
	{
		::RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = ::CULL_MODE_NONE;

		::DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = false;
		depthStateDesc.mDepthWrite = false;

		::PipelineDesc desc = {};
		desc.mType = ::PIPELINE_TYPE_GRAPHICS;
		::GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = ::PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &g_State->swapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = g_State->swapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = g_State->swapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = g_State->depthBuffer->mFormat;
		pipelineSettings.pShaderProgram = g_State->toneMapping;
		pipelineSettings.pVertexLayout = NULL;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.mVRFoveatedRendering = false;
		::addPipeline(g_State->renderer, &desc, &g_State->toneMappingPipeline);
	}
}

void RemovePipelines()
{
	::removePipeline(g_State->renderer, g_State->uberPipeline);
	::removePipeline(g_State->renderer, g_State->deferredShadingPipeline);
	::removePipeline(g_State->renderer, g_State->downsamplePipeline);
	::removePipeline(g_State->renderer, g_State->upsamplePipeline);
	::removePipeline(g_State->renderer, g_State->toneMappingPipeline);
}

void AddGeometry()
{
	const uint32_t maxVertices = 256 * 1024;
	const uint32_t maxIndices = 1024 * 1024;

	g_State->geometry.vertices = (MeshVertex*)tf_malloc(sizeof(MeshVertex) * maxVertices);
	ASSERT(g_State->geometry.vertices);
	memset(g_State->geometry.vertices, 0, sizeof(MeshVertex) * maxVertices);

	g_State->geometry.indices = (uint32_t*)tf_malloc(sizeof(uint32_t) * maxIndices);
	ASSERT(g_State->geometry.indices);
	memset(g_State->geometry.indices, 0, sizeof(uint32_t) * maxIndices);

	g_State->meshes = (GPUMesh*)tf_malloc(sizeof(GPUMesh) * k_MeshesMaxCount);
	ASSERT(g_State->meshes);
	memset(g_State->meshes, 0, sizeof(GPUMesh) * k_MeshesMaxCount);
	g_State->meshCount = 0;

	GPUMesh* plane = &g_State->meshes[(size_t)Meshes::Plane];
	const char* planePath = "Content/Models/Plane.obj";
	LoadMesh(&g_State->geometry, planePath, plane);

	GPUMesh* cube = &g_State->meshes[(size_t)Meshes::Cube];
	const char* cubePath = "Content/Models/Cube.obj";
	LoadMesh(&g_State->geometry, cubePath, cube);

	GPUMesh* helmet = &g_State->meshes[(size_t)Meshes::DamagedHelmet];
	const char* helmetPath = "Content/Models/DamagedHelmet.obj";
	LoadMesh(&g_State->geometry, helmetPath, helmet);

	g_State->meshCount = (uint32_t)Meshes::_Count;

	{
		::BufferLoadDesc meshDesc = {};
		meshDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
		meshDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		meshDesc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
		meshDesc.mDesc.mSize = sizeof(GPUMesh) * k_MeshesMaxCount;
		meshDesc.mDesc.mElementCount = (uint32_t)(meshDesc.mDesc.mSize / sizeof(uint32_t));
		meshDesc.mDesc.bBindless = true;
		meshDesc.pData = g_State->meshes;
		meshDesc.ppBuffer = &g_State->meshesBuffer;
		meshDesc.mDesc.pName = "Mesh Buffer";
		::addResource(&meshDesc, NULL);

		::BufferLoadDesc vbDesc = {};
		vbDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
		vbDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		vbDesc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
		vbDesc.mDesc.mSize = sizeof(MeshVertex) * g_State->geometry.vertexCount;
		vbDesc.mDesc.mElementCount = (uint32_t)(vbDesc.mDesc.mSize / sizeof(uint32_t));
		vbDesc.mDesc.bBindless = true;
		vbDesc.pData = g_State->geometry.vertices;
		vbDesc.ppBuffer = &g_State->vertexBuffer;
		vbDesc.mDesc.pName = "Vertex Buffer";
		::addResource(&vbDesc, NULL);

		::BufferLoadDesc ibDesc = {};
		ibDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_INDEX_BUFFER;
		ibDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		ibDesc.mDesc.mSize = sizeof(uint32_t) * g_State->geometry.indexCount;
		ibDesc.pData = g_State->geometry.indices;
		ibDesc.ppBuffer = &g_State->indexBuffer;
		::addResource(&ibDesc, NULL);
	}
}

void RemoveGeometry()
{
	::removeResource(g_State->meshesBuffer);
	::removeResource(g_State->vertexBuffer);
	::removeResource(g_State->indexBuffer);

	tf_free(g_State->geometry.indices);
	tf_free(g_State->geometry.vertices);
	tf_free(g_State->meshes);

	k_ScratchGeometryData.destroy();
}

static inline void loadMat4(const ::mat4& matrix, float* output)
{
	output[0] = matrix.getCol(0).getX();
	output[1] = matrix.getCol(0).getY();
	output[2] = matrix.getCol(0).getZ();
	output[3] = matrix.getCol(0).getW();
	output[4] = matrix.getCol(1).getX();
	output[5] = matrix.getCol(1).getY();
	output[6] = matrix.getCol(1).getZ();
	output[7] = matrix.getCol(1).getW();
	output[8] = matrix.getCol(2).getX();
	output[9] = matrix.getCol(2).getY();
	output[10] = matrix.getCol(2).getZ();
	output[11] = matrix.getCol(2).getW();
	output[12] = matrix.getCol(3).getX();
	output[13] = matrix.getCol(3).getY();
	output[14] = matrix.getCol(3).getZ();
	output[15] = matrix.getCol(3).getW();
}

int32_t mikkt_GetNumFaces(const SMikkTSpaceContext* context);
int32_t mikkt_GetNumVerticesOfFace(const SMikkTSpaceContext* context, int32_t faceIndex);
uint32_t mikkt_GetVertexIndex(const SMikkTSpaceContext* context, int32_t faceIndex, int32_t vertIndex);
void mikkt_GetPosition(const SMikkTSpaceContext* context, float position[3], int32_t faceIndex, int32_t vertIndex);
void mikkt_GetNormal(const SMikkTSpaceContext* context, float normal[3], int32_t faceIndex, int32_t vertIndex);
void mikkt_GetTexcoord(const SMikkTSpaceContext* context, float normal[2], int32_t faceIndex, int32_t vertIndex);
void mikkt_SetTSpaceBasic(const SMikkTSpaceContext* context, const float tangent[3], float sign, int32_t faceIndex, int32_t vertIndex);

struct MikkTUserData
{
	RendererGeometry* geometry;
};

void LoadMesh(RendererGeometry* geometry, const char* path, GPUMesh* mesh)
{
	k_ScratchGeometryData.initialize();
	k_ScratchGeometryData.reset();

	fastObjMesh* obj = fast_obj_read(path);
	if (!obj)
	{
		return;
	}

	size_t indexCount = 0;
	for (uint32_t i = 0; i < obj->face_count; ++i)
	{
		indexCount += 3 * (obj->face_vertices[i] - 2);
	}

	size_t vertexOffset = 0;
	size_t indexOffset = 0;

	for (uint32_t i = 0; i < obj->face_count; ++i)
	{
		ASSERT(obj->face_vertices[i] == 3);

		for (uint32_t j = 0; j < obj->face_vertices[i]; ++j)
		{
			fastObjIndex gi = obj->indices[indexOffset + j];

			MeshVertex* v = &k_ScratchGeometryData.geometry.vertices[vertexOffset++];
			v->position.x = obj->positions[gi.p * 3 + 0];
			v->position.y = obj->positions[gi.p * 3 + 1];
			v->position.z = obj->positions[gi.p * 3 + 2];
			v->color.x = obj->colors[gi.p * 3 + 0];
			v->color.y = obj->colors[gi.p * 3 + 1];
			v->color.z = obj->colors[gi.p * 3 + 2];
			v->normal.x = obj->normals[gi.n * 3 + 0];
			v->normal.y = obj->normals[gi.n * 3 + 1];
			v->normal.z = obj->normals[gi.n * 3 + 2];
			v->uv.x = obj->texcoords[gi.t * 2 + 0];
			v->uv.y = 1.0f - obj->texcoords[gi.t * 2 + 1];
			v->tangent.x = 0;
			v->tangent.z = 0;
			v->tangent.y = 0;
			v->tangent.w = 0;
		}

		indexOffset += obj->face_vertices[i];
	}

	ASSERT(vertexOffset == indexCount);
	k_ScratchGeometryData.geometry.vertexCount = (uint32_t)indexCount;
	k_ScratchGeometryData.geometry.indexCount = (uint32_t)indexCount;

	for (uint32_t i = 0; i < indexCount; ++i) {
		k_ScratchGeometryData.geometry.indices[i] = (uint32_t)i;
	}

	// Calculate MikkTSpace tangents
	::SMikkTSpaceInterface mikktInterface = {};
	mikktInterface.m_getNumFaces = mikkt_GetNumFaces;
	mikktInterface.m_getNumVerticesOfFace = mikkt_GetNumVerticesOfFace;
	mikktInterface.m_getPosition = mikkt_GetPosition;
	mikktInterface.m_getNormal = mikkt_GetNormal;
	mikktInterface.m_getTexCoord = mikkt_GetTexcoord;
	mikktInterface.m_setTSpaceBasic = mikkt_SetTSpaceBasic;

	::SMikkTSpaceContext mikktContext = {};
	mikktContext.m_pInterface = &mikktInterface;
	mikktContext.m_pUserData = (void*)&k_ScratchGeometryData.geometry;

	::genTangSpaceDefault(&mikktContext);

	fast_obj_destroy(obj);

	// TODO: Regenerate the index buffer with meshoptimizer

	for (uint32_t i = 0; i < k_ScratchGeometryData.geometry.vertexCount; ++i)
	{
		geometry->vertices[geometry->vertexCount + i] = k_ScratchGeometryData.geometry.vertices[i];
	}

	for (uint32_t i = 0; i < k_ScratchGeometryData.geometry.indexCount; ++i) {
		geometry->indices[geometry->indexCount + i] = k_ScratchGeometryData.geometry.indices[i];
	}

	mesh->indexOffset = geometry->indexCount;
	mesh->vertexOffset = geometry->vertexCount;
	mesh->indexCount = k_ScratchGeometryData.geometry.indexCount;

	geometry->vertexCount += k_ScratchGeometryData.geometry.vertexCount;
	geometry->indexCount += k_ScratchGeometryData.geometry.indexCount;

	return;
}

bool ScratchGeometryData::isInitialized()
{
	return geometry.vertices != NULL && geometry.indices != NULL;
}

void ScratchGeometryData::initialize()
{
	if (isInitialized()) return;
	ASSERT(verticesMaxCount > 0);
	ASSERT(indicexMaxCount > 0);

	geometry.vertices = (MeshVertex*)tf_malloc(sizeof(MeshVertex) * verticesMaxCount);
	geometry.indices = (uint32_t*)tf_malloc(sizeof(uint32_t) * indicexMaxCount);
	reset();
}

void ScratchGeometryData::reset()
{
	ASSERT(verticesMaxCount > 0);
	ASSERT(indicexMaxCount > 0);

	ASSERT(geometry.vertices);
	memset(geometry.vertices, 0, sizeof(MeshVertex) * verticesMaxCount);
	geometry.vertexCount = 0;

	ASSERT(geometry.indices);
	memset(geometry.indices, 0, sizeof(uint32_t) * indicexMaxCount);
	geometry.indexCount = 0;
}

void ScratchGeometryData::destroy()
{
	tf_free(geometry.vertices);
	tf_free(geometry.indices);
}

int32_t mikkt_GetNumFaces(const SMikkTSpaceContext* context)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;
	return (int32_t)geometry->indexCount / 3;
}

int32_t mikkt_GetNumVerticesOfFace(const SMikkTSpaceContext* context, int32_t faceIndex)
{
	(void)context;
	(void)faceIndex;

	return 3;
}

uint32_t mikkt_GetVertexIndex(const SMikkTSpaceContext* context, int32_t faceIndex, int32_t vertIndex)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;

	uint32_t index = faceIndex * 3 + vertIndex;
	ASSERT(index < geometry->indexCount);

	uint32_t vertexIndex = geometry->indices[index];
	ASSERT(vertexIndex < geometry->vertexCount);

	return vertexIndex;
}

void mikkt_GetPosition(const SMikkTSpaceContext* context, float position[3], int32_t faceIndex, int32_t vertIndex)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;

	uint32_t vertexIndex = mikkt_GetVertexIndex(context, faceIndex, vertIndex);
	const MeshVertex& vertex = geometry->vertices[vertexIndex];
	position[0] = vertex.position.x;
	position[1] = vertex.position.y;
	position[2] = vertex.position.z;
}

void mikkt_GetNormal(const SMikkTSpaceContext* context, float normal[3], int32_t faceIndex, int32_t vertIndex)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;

	uint32_t vertexIndex = mikkt_GetVertexIndex(context, faceIndex, vertIndex);
	const MeshVertex& vertex = geometry->vertices[vertexIndex];
	normal[0] = vertex.normal.x;
	normal[1] = vertex.normal.y;
	normal[2] = vertex.normal.z;
}

void mikkt_GetTexcoord(const SMikkTSpaceContext* context, float texcoord[2], int32_t faceIndex, int32_t vertIndex)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;

	uint32_t vertexIndex = mikkt_GetVertexIndex(context, faceIndex, vertIndex);
	const MeshVertex& vertex = geometry->vertices[vertexIndex];
	texcoord[0] = vertex.uv.x;
	texcoord[1] = vertex.uv.y;
}

void mikkt_SetTSpaceBasic(const SMikkTSpaceContext* context, const float tangent[3], float sign, int32_t faceIndex, int32_t vertIndex)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;

	uint32_t vertexIndex = mikkt_GetVertexIndex(context, faceIndex, vertIndex);
	MeshVertex& vertex = geometry->vertices[vertexIndex];
	vertex.tangent.x = tangent[0];
	vertex.tangent.y = tangent[1];
	vertex.tangent.z = tangent[2];
	vertex.tangent.w = sign;
}