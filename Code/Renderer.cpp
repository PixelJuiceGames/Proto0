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

const uint32_t kDataBufferCount = 2;
const uint32_t kDownsampleSteps = 8;
const uint32_t kUpsampleSteps = 7;

const uint32_t k_MaxMaterials = 1024;
const uint32_t k_MaxMeshes = 1024;
const uint32_t k_MaxInstances = 1024 * 1024;
const uint32_t k_MaxIndirectDrawIndexArgs = 1024;

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

	uint32_t numVertices = 0;
	uint32_t numIndices = 0;
};
struct ScratchGeometryData
{
	RendererGeometry geometry = {};
	uint32_t maxVertices = 256 * 1024;
	uint32_t maxIndices = 1024 * 1024;

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
	::RenderTarget* sceneColor = NULL;
	::RenderTarget* depthBuffer = NULL;

	::Buffer* downsampleUniformBuffers[kDownsampleSteps] = { NULL };
	::Buffer* upsampleUniformBuffers[kUpsampleSteps] = { NULL };
	::Texture* bloomDownsamples[kDownsampleSteps] = { NULL };
	::Texture* bloomUpsamples[kUpsampleSteps] = { NULL };

	RendererGeometry geometry = {};
	::Buffer* meshesBuffer = NULL;
	::Buffer* vertexBuffer = NULL;
	::Buffer* indexBuffer = NULL;

	::Buffer* frameUniformBuffers[kDataBufferCount] = { NULL };
	::Buffer* instanceBuffers[kDataBufferCount] = { NULL };
	::Buffer* materialBuffers[kDataBufferCount] = { NULL };
	::Buffer* lightBuffers[kDataBufferCount] = { NULL };
	::Buffer* indirectDrawBuffers[kDataBufferCount] = { NULL };

	GPUMesh* meshes = NULL;
	uint32_t numMeshes = 0;

	GPUInstance* instances = NULL;
	uint32_t numInstances = 0;

	GPUMaterial* materials = NULL;
	uint32_t numMaterials = 0;

	IndirectDrawIndexArguments* indirectDrawIndexArgs[kDataBufferCount] = { NULL };
	uint32_t maxIndirectDrawIndexArgs = 0;
	uint32_t numIndirectDrawIndexArgs[kDataBufferCount] = { 0 };

	GPULight* lights = NULL;
	uint32_t numLights = 0;

	// UberShader
	::Shader* uberShader = NULL;
	::Pipeline* uberPipeline = NULL;

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

	// NOTE: This is the data of a specific material
	::DescriptorSet* uberPersistentDescriptorSet = NULL;
	::DescriptorSet* uberPerFrameDescriptorSet = NULL;

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
			cmdRingDesc.mPoolCount = kDataBufferCount;
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
			for (uint32_t i = 0; i < kDataBufferCount; ++i)
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
			for (uint32_t i = 0; i < kDownsampleSteps; ++i)
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
			for (uint32_t i = 0; i < kUpsampleSteps; ++i)
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

		// Temporary instances, materials and lights
		{
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

				g_State->materials = (GPUMaterial*)tf_malloc(sizeof(GPUMaterial) * k_MaxMaterials);
				ASSERT(g_State->materials);
				memset(g_State->materials, 0, sizeof(GPUMaterial) * k_MaxMaterials);

				GPUMaterial& playerMaterial = g_State->materials[g_State->numMaterials++];
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

				GPUMaterial& gridMaterial = g_State->materials[g_State->numMaterials++];
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

				GPUMaterial& damagedHelmetMaterial = g_State->materials[g_State->numMaterials++];
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
				desc.mDesc.mSize = sizeof(GPUMaterial) * k_MaxMaterials;
				desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
				desc.mDesc.bBindless = true;
				desc.pData = g_State->materials;
				desc.mDesc.pName = "Materials Buffer";
				for (uint32_t i = 0; i < kDataBufferCount; ++i)
				{
					desc.ppBuffer = &g_State->materialBuffers[i];
					::addResource(&desc, NULL);
				}
			}

			// Instances
			{
				g_State->instances = (GPUInstance*)tf_malloc(sizeof(GPUInstance) * k_MaxInstances);
				ASSERT(g_State->instances);
				memset(g_State->instances, 0, sizeof(GPUInstance)* k_MaxInstances);

				g_State->maxIndirectDrawIndexArgs = k_MaxIndirectDrawIndexArgs;

				for (uint32_t i = 0; i < kDataBufferCount; ++i)
				{
					g_State->indirectDrawIndexArgs[i] = (::IndirectDrawIndexArguments*)tf_malloc(sizeof(::IndirectDrawIndexArguments) * g_State->maxIndirectDrawIndexArgs);
					ASSERT(g_State->indirectDrawIndexArgs[i]);
					memset(g_State->indirectDrawIndexArgs[i], 0, sizeof(::IndirectDrawIndexArguments) * g_State->maxIndirectDrawIndexArgs);
					g_State->numIndirectDrawIndexArgs[i] = 0;
				}

				// NOTE: The first entity is the player
				// Player Instance
				{
					uint32_t startInstance = g_State->numInstances;
					ASSERT(startInstance == 0);
					uint32_t meshIndex = (uint32_t)Meshes::Cube;
					GPUInstance& instance = g_State->instances[g_State->numInstances++];
					::mat4 translate = ::mat4::translation({ 0.0f, 0.0f, 1.0f });
					::mat4 scale = ::mat4::scale({ 0.25f, 0.25f, 1.0f });
					loadMat4(translate * scale, &instance.worldMat.m[0]);
					instance.meshIndex = meshIndex;
					instance.materialBufferIndex = 0;

					const GPUMesh& mesh = g_State->meshes[meshIndex];
					for (uint32_t i = 0; i < kDataBufferCount; ++i)
					{
						::IndirectDrawIndexArguments* drawIndexArgs = &g_State->indirectDrawIndexArgs[i][g_State->numIndirectDrawIndexArgs[i]++];
						drawIndexArgs->mIndexCount = mesh.indexCount;
						drawIndexArgs->mStartIndex = mesh.indexOffset;
						drawIndexArgs->mVertexOffset = mesh.vertexOffset;
						drawIndexArgs->mInstanceCount = 1;
						drawIndexArgs->mStartInstance = startInstance;
					}
				}

				// TODO: Do not generate these here, but in the gameplay code (once we have it)
				// Plane Instances for the ground
				{
					uint32_t startInstance = g_State->numInstances;
					uint32_t numInstances = 0;
					uint32_t meshIndex = (uint32_t)Meshes::Plane;
					for (int32_t y = -50; y < 50; y++)
					{
						for (int32_t x = -50; x < 50; x++)
						{
							GPUInstance& instance = g_State->instances[g_State->numInstances++];
							::mat4 translate = ::mat4::translation({ x + 0.5f, y + 0.5f, 0.0f });
							loadMat4(translate, &instance.worldMat.m[0]);
							instance.meshIndex = meshIndex;
							instance.materialBufferIndex = 1;
							numInstances++;
						}
					}

					const GPUMesh& mesh = g_State->meshes[meshIndex];

					for (uint32_t i = 0; i < kDataBufferCount; ++i)
					{
						::IndirectDrawIndexArguments* drawIndexArgs = &g_State->indirectDrawIndexArgs[i][g_State->numIndirectDrawIndexArgs[i]++];
						drawIndexArgs->mIndexCount = mesh.indexCount;
						drawIndexArgs->mStartIndex = mesh.indexOffset;
						drawIndexArgs->mVertexOffset = mesh.vertexOffset;
						drawIndexArgs->mInstanceCount = numInstances;
						drawIndexArgs->mStartInstance = startInstance;
					}
				}

				//// Damaged Helmet Instance
				//{
				//	uint32_t startInstance = g_State->numInstances;
				//	GPUInstance& instance = g_State->instances[g_State->numInstances++];
				//	::mat4 translate = ::mat4::translation({ 0.0f, 0.0f, 0.75f });
				//	loadMat4(translate, &instance.worldMat.m[0]);
				//	instance.meshIndex = (uint32_t)Meshes::DamagedHelmet;
				//	instance.materialBufferIndex = 1;

				//	const GPUMesh& mesh = g_State->meshes[instance.meshIndex];

				//	for (uint32_t i = 0; i < kDataBufferCount; ++i)
				//	{
				//		::IndirectDrawIndexArguments* drawIndexArgs = &g_State->indirectDrawIndexArgs[i][g_State->numIndirectDrawIndexArgs[i]++];
				//		drawIndexArgs->mIndexCount = mesh.indexCount;
				//		drawIndexArgs->mStartIndex = mesh.indexOffset;
				//		drawIndexArgs->mVertexOffset = mesh.vertexOffset;
				//		drawIndexArgs->mInstanceCount = 2;
				//		drawIndexArgs->mStartInstance = startInstance;
				//	}
				//}

				::BufferLoadDesc desc = {};
				desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
				desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
				desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
				desc.mDesc.mSize = sizeof(GPUInstance) * k_MaxInstances;
				desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
				desc.mDesc.bBindless = true;
				desc.pData = g_State->instances;
				desc.mDesc.pName = "Instances Buffer";
				for (uint32_t i = 0; i < kDataBufferCount; ++i)
				{
					desc.ppBuffer = &g_State->instanceBuffers[i];
					::addResource(&desc, NULL);
				}

				// Indirect draw args buffers
				{
					::BufferLoadDesc desc = {};
					desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_INDIRECT_BUFFER;
					desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
					desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
					desc.mDesc.mSize = sizeof(::IndirectDrawIndexArguments) * g_State->maxIndirectDrawIndexArgs;
					desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
					desc.mDesc.bBindless = false;
					desc.mDesc.mStartState = ::RESOURCE_STATE_INDIRECT_ARGUMENT;
					desc.mDesc.pName = "Indirect Draw Buffer";

					for (uint32_t i = 0; i < kDataBufferCount; ++i)
					{
						desc.pData = g_State->indirectDrawIndexArgs[i];
						desc.ppBuffer = &g_State->indirectDrawBuffers[i];
						::addResource(&desc, NULL);
					}
				}
			}

			// Lights
			{
				g_State->numLights = 3;
				g_State->lights = (GPULight*)tf_malloc(sizeof(GPULight) * g_State->numLights);
				ASSERT(g_State->lights);
				memset(g_State->lights, 0, sizeof(GPULight)* g_State->numLights);

				// Key light
				GPULight& keyLight = g_State->lights[0];
				keyLight.position = { 4.0f, -4.0f, 5.5f };
				keyLight.range = 15.0f;
				keyLight.color = ::srgbToLinearf3({ 1.0f, 0.2f, 0.2f });
				keyLight.intensity = 10.0f;

				// Fill light
				GPULight& fillLight = g_State->lights[1];
				fillLight.position = { -4.0f, -3.0f, 5.5f };
				fillLight.range = 15.0f;
				fillLight.color = ::srgbToLinearf3({ 0.2f, 0.2f, 1.0f });
				fillLight.intensity = 5.0f;

				// Back light
				GPULight& backLight = g_State->lights[2];
				backLight.position = { 0.0f, 3.0f, 8.0f };
				backLight.range = 15.0f;
				backLight.color = ::srgbToLinearf3({ 0.2f, 1.0, 0.2f });
				backLight.intensity = 10.0f;

				::BufferLoadDesc desc = {};
				desc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
				desc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
				desc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
				desc.mDesc.mSize = sizeof(GPUInstance) * g_State->numLights;
				desc.mDesc.mElementCount = (uint32_t)(desc.mDesc.mSize / sizeof(uint32_t));
				desc.mDesc.bBindless = true;
				desc.pData = g_State->lights;
				desc.mDesc.pName = "Lights Buffer";
				for (uint32_t i = 0; i < kDataBufferCount; ++i)
				{
					desc.ppBuffer = &g_State->lightBuffers[i];
					::addResource(&desc, NULL);
				}
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

		for (uint32_t i = 0; i < kDataBufferCount; ++i)
		{
			::removeResource(g_State->frameUniformBuffers[i]);
			::removeResource(g_State->materialBuffers[i]);
			::removeResource(g_State->instanceBuffers[i]);
			::removeResource(g_State->lightBuffers[i]);
		}

		for (uint32_t i = 0; i < kDataBufferCount; ++i)
		{
			::removeResource(g_State->indirectDrawBuffers[i]);
			tf_free(g_State->indirectDrawIndexArgs[i]);
		}

		for (uint32_t i = 0; i < kDownsampleSteps; ++i)
		{
			::removeResource(g_State->downsampleUniformBuffers[i]);
		}

		for (uint32_t i = 0; i < kUpsampleSteps; ++i)
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

		// Forward Pass
		{
			// Resource Barriers
			{
				::RenderTargetBarrier rtBarriers[] = {
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
				bindRenderTargets.mDepthStencil.mLoadAction = ::LOAD_ACTION_CLEAR;
				bindRenderTargets.mDepthStencil.pDepthStencil = g_State->depthBuffer;
				::cmdBindRenderTargets(cmd, &bindRenderTargets);
			}

			::cmdSetViewport(cmd, 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f);
			::cmdSetScissor(cmd, 0, 0, (uint32_t)windowWidth, (uint32_t)windowHeight);

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
				::mat4 scale = ::mat4::scale({ 0.25f, 0.25f, 1.0f });
				loadMat4(translate * scale, &instance.worldMat.m[0]);
				instance.meshIndex = meshIndex;
				instance.materialBufferIndex = 0;

				::BufferUpdateDesc updateDesc = {};
				updateDesc.pBuffer = g_State->instanceBuffers[g_State->frameIndex];
				updateDesc.mDstOffset = 0; // The player instance is at the start of the instance buffer
				updateDesc.mSize = sizeof(GPUInstance);
				::beginUpdateResource(&updateDesc);
				memcpy(updateDesc.pMappedData, &instance, sizeof(GPUInstance));
				::endUpdateResource(&updateDesc);
			}

			// Render meshes
			{
				::mat4 projMat = ::mat4::perspectiveRH(1.0471f, windowHeight / (float)windowWidth, 100.0f, 0.01f);
				::mat4 projViewMat = projMat * scene->playerCamera.viewMatrix;
				Frame frameData = {};
				loadMat4(projViewMat, &frameData.projViewMat.m[0]);
				frameData.sunColor = { 0.0f, 0.0f, 0.0f, 0.0f };
				frameData.sunDirection = { 0.0f, 0.0f, 0.0f, 0.0f };
				frameData.meshBufferIndex = (uint32_t)g_State->meshesBuffer->mDx.mDescriptors;
				frameData.vertexBufferIndex = (uint32_t)g_State->vertexBuffer->mDx.mDescriptors;
				frameData.materialBufferIndex = (uint32_t)g_State->materialBuffers[g_State->frameIndex]->mDx.mDescriptors;
				frameData.instanceBufferIndex = (uint32_t)g_State->instanceBuffers[g_State->frameIndex]->mDx.mDescriptors;
				frameData.lightBufferIndex = (uint32_t)g_State->lightBuffers[g_State->frameIndex]->mDx.mDescriptors;
				frameData.numLights = g_State->numLights;

				::BufferUpdateDesc desc = { g_State->frameUniformBuffers[g_State->frameIndex] };
				::beginUpdateResource(&desc);
				memcpy(desc.pMappedData, &frameData, sizeof(frameData));
				::endUpdateResource(&desc);

				::cmdBindPipeline(cmd, g_State->uberPipeline);
				::cmdBindDescriptorSet(cmd, 0, g_State->uberPersistentDescriptorSet);
				::cmdBindDescriptorSet(cmd, g_State->frameIndex, g_State->uberPerFrameDescriptorSet);
				::cmdBindIndexBuffer(cmd, g_State->indexBuffer, ::INDEX_TYPE_UINT32, 0);

				::cmdExecuteIndirect(cmd, ::INDIRECT_DRAW_INDEX, 2, g_State->indirectDrawBuffers[g_State->frameIndex], 0, NULL, 0);
			}

			::cmdBindRenderTargets(cmd, NULL);
		}

		// Bloom
		{
			// Downsample
			for (uint32_t i = 0; i < kDownsampleSteps; ++i)
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
			for (uint32_t i = 0; i < kUpsampleSteps; ++i)
			{
				UpsampleUniform uniform = {};
				uniform.radius = 0.75f;

				if (i == 0)
				{
					uniform.inputSize = { g_State->bloomDownsamples[kDownsampleSteps - 1]->mWidth, g_State->bloomDownsamples[kDownsampleSteps - 1]->mHeight };

					// Resource Barriers
					{
						::TextureBarrier tBarriers[] = {
							{ g_State->bloomDownsamples[kDownsampleSteps - 1], ::RESOURCE_STATE_UNORDERED_ACCESS, ::RESOURCE_STATE_SHADER_RESOURCE },
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
					{ g_State->bloomUpsamples[kUpsampleSteps - 1], ::RESOURCE_STATE_UNORDERED_ACCESS, ::RESOURCE_STATE_SHADER_RESOURCE }
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

		g_State->frameIndex = (g_State->frameIndex + 1) % kDataBufferCount;
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
		desc.mStartState = ::RESOURCE_STATE_DEPTH_WRITE;
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
		for (uint32_t i = 0; i < kDownsampleSteps; i++)
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
		for (uint32_t i = 0; i < kUpsampleSteps; ++i)
		{
			desc.mWidth = (uint32_t)windowWidth >> (kUpsampleSteps - i);
			desc.mHeight = (uint32_t)windowHeight >> (kUpsampleSteps - i);
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

	for (uint32_t i = 0; i < kDownsampleSteps; i++)
	{
		::removeResource(g_State->bloomDownsamples[i]);
	}

	for (uint32_t i = 0; i < kUpsampleSteps; i++)
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
		desc.mMaxSets = kDataBufferCount;
		desc.mNodeIndex = 0;
		desc.mDescriptorCount = 1;
		desc.pDescriptors = SRT_UberShaderData::PerFramePtr();
		::addDescriptorSet(g_State->renderer, &desc, &g_State->uberPerFrameDescriptorSet);
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
		desc.mMaxSets = kDownsampleSteps;
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
		desc.mMaxSets = kUpsampleSteps;
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
		desc.mMaxSets = kDataBufferCount;
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

	for (uint32_t i = 0; i < kDataBufferCount; ++i)
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_UberShaderData::PerFrame, CB0)) / sizeof(::Descriptor);
		uParams[0].ppBuffers = &g_State->frameUniformBuffers[i];
		updateDescriptorSet(g_State->renderer, i, g_State->uberPerFrameDescriptorSet, 1, uParams);
	}

	// Downsample Descriptor Sets
	{
		DescriptorData uParams[1] = {};
		uParams[0].mIndex = (offsetof(SRT_DownsampleData::Persistent, gLinearClampSampler)) / sizeof(::Descriptor);
		uParams[0].ppSamplers = &g_State->linearClampSampler;
		updateDescriptorSet(g_State->renderer, 0, g_State->downsamplePersistentDescriptorSet, 1, uParams);
	}

	for (uint32_t i = 0; i < kDownsampleSteps; ++i)
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

	for (uint32_t i = 0; i < kUpsampleSteps; ++i)
	{
		DescriptorData uParams[4] = {};
		uParams[0].mIndex = (offsetof(SRT_UpsampleData::PerDraw, CB0)) / sizeof(::Descriptor);
		uParams[0].ppBuffers = &g_State->upsampleUniformBuffers[i];
		uParams[1].mIndex = (offsetof(SRT_UpsampleData::PerDraw, gSourceTexture)) / sizeof(::Descriptor);
		uParams[1].ppTextures = &g_State->bloomDownsamples[kUpsampleSteps - (i + 1)];
		uParams[2].mIndex = (offsetof(SRT_UpsampleData::PerDraw, gPreviousTexture)) / sizeof(::Descriptor);
		if (i == 0)
		{
			uParams[2].ppTextures = &g_State->bloomDownsamples[kDownsampleSteps - 1];
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

	for (uint32_t i = 0; i < kDataBufferCount; ++i)
	{
		DescriptorData uParams[3] = {};
		uParams[0].mIndex = (offsetof(SRT_ToneMappingData::PerFrame, gSceneColor)) / sizeof(::Descriptor);
		uParams[0].ppTextures = &g_State->sceneColor->pTexture;
		uParams[1].mIndex = (offsetof(SRT_ToneMappingData::PerFrame, gTonyMcMapfaceLut)) / sizeof(::Descriptor);
		uParams[1].ppTextures = &g_State->tonyMcMapfaceLUT;
		uParams[2].mIndex = (offsetof(SRT_ToneMappingData::PerFrame, gBloomBuffer)) / sizeof(::Descriptor);
		uParams[2].ppTextures = &g_State->bloomUpsamples[kUpsampleSteps - 1];
		updateDescriptorSet(g_State->renderer, i, g_State->toneMappingPerFrameDescriptorSet, 3, uParams);
	}
}

void RemoveDescriptorSets()
{
	::removeDescriptorSet(g_State->renderer, g_State->uberPersistentDescriptorSet);
	::removeDescriptorSet(g_State->renderer, g_State->uberPerFrameDescriptorSet);
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

		::TinyImageFormat colorFormats = { ::TinyImageFormat_R16G16B16A16_SFLOAT };

		::PipelineDesc desc = {};
		desc.mType = ::PIPELINE_TYPE_GRAPHICS;
		::GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = ::PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &colorFormats;
		pipelineSettings.mSampleCount = ::SAMPLE_COUNT_1;
		pipelineSettings.mSampleQuality = 0;
		pipelineSettings.mDepthStencilFormat = g_State->depthBuffer->mFormat;
		pipelineSettings.pShaderProgram = g_State->uberShader;
		pipelineSettings.pVertexLayout = NULL;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.mVRFoveatedRendering = false;
		::addPipeline(g_State->renderer, &desc, &g_State->uberPipeline);
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

	g_State->meshes = (GPUMesh*)tf_malloc(sizeof(GPUMesh) * k_MaxMeshes);
	ASSERT(g_State->meshes);
	memset(g_State->meshes, 0, sizeof(GPUMesh) * k_MaxMeshes);
	g_State->numMeshes = 0;

	GPUMesh* plane = &g_State->meshes[(size_t)Meshes::Plane];
	const char* planePath = "Content/Models/Plane.obj";
	LoadMesh(&g_State->geometry, planePath, plane);

	GPUMesh* cube = &g_State->meshes[(size_t)Meshes::Cube];
	const char* cubePath = "Content/Models/Cube.obj";
	LoadMesh(&g_State->geometry, cubePath, cube);

	GPUMesh* helmet = &g_State->meshes[(size_t)Meshes::DamagedHelmet];
	const char* helmetPath = "Content/Models/DamagedHelmet.obj";
	LoadMesh(&g_State->geometry, helmetPath, helmet);

	g_State->numMeshes = (uint32_t)Meshes::_Count;

	{
		::BufferLoadDesc meshDesc = {};
		meshDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_BUFFER_RAW;
		meshDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		meshDesc.mDesc.mFlags = ::BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS;
		meshDesc.mDesc.mSize = sizeof(GPUMesh) * k_MaxMeshes;
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
		vbDesc.mDesc.mSize = sizeof(MeshVertex) * g_State->geometry.numVertices;
		vbDesc.mDesc.mElementCount = (uint32_t)(vbDesc.mDesc.mSize / sizeof(uint32_t));
		vbDesc.mDesc.bBindless = true;
		vbDesc.pData = g_State->geometry.vertices;
		vbDesc.ppBuffer = &g_State->vertexBuffer;
		vbDesc.mDesc.pName = "Vertex Buffer";
		::addResource(&vbDesc, NULL);

		::BufferLoadDesc ibDesc = {};
		ibDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_INDEX_BUFFER;
		ibDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		ibDesc.mDesc.mSize = sizeof(uint32_t) * g_State->geometry.numIndices;
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
	k_ScratchGeometryData.geometry.numVertices = (uint32_t)indexCount;
	k_ScratchGeometryData.geometry.numIndices = (uint32_t)indexCount;

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

	for (uint32_t i = 0; i < k_ScratchGeometryData.geometry.numVertices; ++i)
	{
		geometry->vertices[geometry->numVertices + i] = k_ScratchGeometryData.geometry.vertices[i];
	}

	for (uint32_t i = 0; i < k_ScratchGeometryData.geometry.numIndices; ++i) {
		geometry->indices[geometry->numIndices + i] = k_ScratchGeometryData.geometry.indices[i];
	}

	mesh->indexOffset = geometry->numIndices;
	mesh->vertexOffset = geometry->numVertices;
	mesh->indexCount = k_ScratchGeometryData.geometry.numIndices;

	geometry->numVertices += k_ScratchGeometryData.geometry.numVertices;
	geometry->numIndices += k_ScratchGeometryData.geometry.numIndices;

	return;
}

bool ScratchGeometryData::isInitialized()
{
	return geometry.vertices != NULL && geometry.indices != NULL;
}

void ScratchGeometryData::initialize()
{
	if (isInitialized()) return;
	ASSERT(maxVertices > 0);
	ASSERT(maxIndices > 0);

	geometry.vertices = (MeshVertex*)tf_malloc(sizeof(MeshVertex) * maxVertices);
	geometry.indices = (uint32_t*)tf_malloc(sizeof(uint32_t) * maxIndices);
	reset();
}

void ScratchGeometryData::reset()
{
	ASSERT(maxVertices > 0);
	ASSERT(maxIndices > 0);

	ASSERT(geometry.vertices);
	memset(geometry.vertices, 0, sizeof(MeshVertex) * maxVertices);
	geometry.numVertices = 0;

	ASSERT(geometry.indices);
	memset(geometry.indices, 0, sizeof(uint32_t) * maxIndices);
	geometry.numIndices = 0;
}

void ScratchGeometryData::destroy()
{
	tf_free(geometry.vertices);
	tf_free(geometry.indices);
}

int32_t mikkt_GetNumFaces(const SMikkTSpaceContext* context)
{
	RendererGeometry* geometry = (RendererGeometry*)context->m_pUserData;
	return (int32_t)geometry->numIndices / 3;
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
	ASSERT(index < geometry->numIndices);

	uint32_t vertexIndex = geometry->indices[index];
	ASSERT(vertexIndex < geometry->numVertices);

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