#include <assert.h>

// SDL3

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// fast_obj

#define FAST_OBJ_IMPLEMENTATION
#include <fast_obj.h>

// The-Forge

#include <Graphics/Interfaces/IGraphics.h>
#include <Utilities/Interfaces/IFileSystem.h>
#include <Utilities/Interfaces/ILog.h>
#include <Utilities/Interfaces/IMemory.h>
#include <Utilities/RingBuffer.h>

// Shader Interop

#include <ShaderGlobals.h>

// TODO: These should be autogenerated with some codegen solution
// from the shader (source of bytecode reflections) 
// Descriptor Sets
struct SRT_UberShaderData
{
	struct PerFrame
	{
		const ::Descriptor CB0 =
		{
#if defined IF_VALIDATE_DESCRIPTOR
			"CB0", ROOT_PARAM_PerFrame, 
#endif
			::DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, 0
		};
	}* pPerFrame;

	static const ::Descriptor* PerFramePtr()
	{
		if (!sizeof(PerFrame))
		{
			return 0;
		}

		static PerFrame layout = {};
		::Descriptor* desc = (::Descriptor*)((uint64_t)&layout);
		return &desc[0];
	}
};

struct MeshVertex
{
	::float3 position;
	::float3 normal;
	::float3 color;
	::float2 uv;
};

struct RendererGeometry
{
	MeshVertex* vertices = NULL;
	uint32_t* indices = NULL;

	uint32_t numVertices = 0;
	uint32_t numIndices = 0;
};

struct AppState
{
	SDL_Window* window = NULL;

	uint32_t dataBufferCount = 2;

	::Renderer* renderer = NULL;
	::Queue* graphicsQueue = NULL;
	::GpuCmdRing graphicsCmdRing = {};
	::SwapChain* swapChain = NULL;
	::Semaphore* imageAcquiredSemaphore = NULL;
	::RenderTarget* depthBuffer = NULL;

	RendererGeometry geometry = {};
	::Buffer* vertexBuffer = NULL;
	::Buffer* indexBuffer = NULL;

	::Shader* uberShader = NULL;
	::DescriptorSet* uniformDescriptorSet = NULL;
	::Pipeline* uberPipeline = NULL; 
};

bool renderer_Initialize(AppState* appState);
void renderer_Exit(AppState* appState);
bool renderer_OnLoad(AppState* appState, ReloadDesc reloadDesc);
void renderer_OnUnload(AppState* appState, ReloadDesc reloadDesc);
bool renderer_AddSwapChain(AppState* appState);
void renderer_RemoveSwapChain(AppState* appState);
bool renderer_AddRenderTargets(AppState* appState);
void renderer_RemoveRenderTargets(AppState* appState);
void renderer_Draw(AppState* appState);
void renderer_LoadGeometry(RendererGeometry* geometry, const char* path);
void renderer_AddShaders(AppState* appState);
void renderer_RemoveShaders(AppState* appState);
void renderer_AddDescriptorSets(AppState* appState);
void renderer_PrepareDescriptorSets(AppState* appState);
void renderer_RemoveDescriptorSets(AppState* appState);
void renderer_AddPipelines(AppState* appState);
void renderer_RemovePipelines(AppState* appState);

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	AppState* as = (AppState*)SDL_calloc(1, sizeof(AppState));
	if (!as)
	{
		return SDL_APP_FAILURE;
	}

	*appstate = as;

	as->window = SDL_CreateWindow("Prototype 0", 1920, 1080, SDL_WINDOW_RESIZABLE);
	if (!as->window)
	{
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if (!renderer_Initialize(as))
	{
		return SDL_APP_FAILURE;
	}
	
	SDL_Log("Initialized");
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	AppState* as = (AppState*)appstate;
	renderer_Draw(as);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	if (appstate != NULL)
	{
		AppState* as = (AppState*)appstate;
		renderer_Exit(as);

		SDL_free(as);
	}
}

bool renderer_Initialize(AppState* appState)
{
	if (!appState)
	{
		SDL_Log("AppState has not been initialized");
		return false;
	}

	appState->dataBufferCount = 2;

	// Initialize Memory Allocation System
	{
		if (!::initMemAlloc("Prototype 0"))
		{
			SDL_Log("Couldn't initialize The-Forge Memory Allocation system");
			return false;
		}
	}

	// Initialize The-Forge File System
	{
		FileSystemInitDesc desc = FileSystemInitDesc{};
		desc.pAppName = "Prototype 0";
		if (!::initFileSystem(&desc))
		{
			SDL_Log("Couldn't initialize The-Forge File system");
			return false;
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
		::initGPUConfiguration(desc.pExtendedSettings);

		::initRenderer("Prototype 0", &desc, &appState->renderer);
		if (!appState->renderer)
		{
			SDL_Log("Couldn't initialize The-Forge Renderer");
			return false;
		}

		::setupGPUConfigurationPlatformParameters(appState->renderer, desc.pExtendedSettings);
	}

	// Initialize Graphics Queue
	{
		::QueueDesc queueDesc = {};
		queueDesc.mType = ::QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = ::QUEUE_FLAG_INIT_MICROPROFILE;
		::initQueue(appState->renderer, &queueDesc, &appState->graphicsQueue);

		::GpuCmdRingDesc cmdRingDesc = {};
		cmdRingDesc.pQueue = appState->graphicsQueue;
		cmdRingDesc.mPoolCount = appState->dataBufferCount;
		cmdRingDesc.mCmdPerPoolCount = 1;
		cmdRingDesc.mAddSyncPrimitives = true;
		::initGpuCmdRing(appState->renderer, &cmdRingDesc, &appState->graphicsCmdRing);

		::initSemaphore(appState->renderer, &appState->imageAcquiredSemaphore);
	}

	::initResourceLoaderInterface(appState->renderer);

	::RootSignatureDesc rootDesc = {};
	rootDesc.pGraphicsFileName = "DefaultRootSignature.rs";
	::initRootSignature(appState->renderer, &rootDesc);

	if (!renderer_OnLoad(appState, { ::RELOAD_TYPE_ALL }))
	{
		SDL_Log("Couldn't load renderer resources");
		return false;
	}

	const uint32_t maxVertices = 256 * 1024;
	const uint32_t maxIndices = 1024 * 1024;

	appState->geometry.vertices = (MeshVertex*)tf_malloc(sizeof(MeshVertex) * maxVertices);
	assert(appState->geometry.vertices);
	memset(appState->geometry.vertices, 0, sizeof(MeshVertex) * maxVertices);

	appState->geometry.indices = (uint32_t*)tf_malloc(sizeof(uint32_t) * maxIndices);
	assert(appState->geometry.indices);
	memset(appState->geometry.indices, 0, sizeof(uint32_t) * maxIndices);

	const char* axisCalibratorPath = "Content/Models/AxisCalibrator.obj";
	renderer_LoadGeometry(&appState->geometry, axisCalibratorPath);

	{
		::BufferLoadDesc vbDesc = {};
		vbDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_VERTEX_BUFFER;
		vbDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		vbDesc.mDesc.mSize = sizeof(MeshVertex) * appState->geometry.numVertices;
		vbDesc.pData = appState->geometry.vertices;
		vbDesc.ppBuffer = &appState->vertexBuffer;
		::addResource(&vbDesc, NULL);

		::BufferLoadDesc ibDesc = {};
		ibDesc.mDesc.mDescriptors = ::DESCRIPTOR_TYPE_INDEX_BUFFER;
		ibDesc.mDesc.mMemoryUsage = ::RESOURCE_MEMORY_USAGE_GPU_ONLY;
		ibDesc.mDesc.mSize = sizeof(uint32_t) * appState->geometry.numIndices;
		ibDesc.pData = appState->geometry.indices;
		ibDesc.ppBuffer = &appState->indexBuffer;
		::addResource(&ibDesc, NULL);

		::waitForAllResourceLoads();
	}

	return true;
}

void renderer_Exit(AppState* appState)
{
	if (!appState)
	{
		SDL_Log("AppState has not been initialized");
		return;
	}

	tf_free(appState->geometry.indices);
	tf_free(appState->geometry.vertices);

	renderer_OnUnload(appState, { ::RELOAD_TYPE_ALL });

	::exitRootSignature(appState->renderer);

	::removeResource(appState->vertexBuffer);
	::removeResource(appState->indexBuffer);

	::exitGpuCmdRing(appState->renderer, &appState->graphicsCmdRing);
	::exitSemaphore(appState->renderer, appState->imageAcquiredSemaphore);
	::exitResourceLoaderInterface(appState->renderer);

	::exitQueue(appState->renderer, appState->graphicsQueue);

	::exitRenderer(appState->renderer);
	::exitGPUConfiguration();
	::exitLog();
	::exitFileSystem();
	::exitMemAlloc();
}

bool renderer_OnLoad(AppState* appState, ::ReloadDesc reloadDesc)
{
	assert(appState);
	assert(appState->renderer);

	if (reloadDesc.mType & ::RELOAD_TYPE_SHADER)
	{
		renderer_AddShaders(appState);
		renderer_AddDescriptorSets(appState);
	}

	if (reloadDesc.mType & (::RELOAD_TYPE_RESIZE | ::RELOAD_TYPE_RENDERTARGET))
	{
		if (!renderer_AddSwapChain(appState))
		{
			return false;
		}

		if (!renderer_AddRenderTargets(appState))
		{
			return false;
		}
	}

	if (reloadDesc.mType & (::RELOAD_TYPE_SHADER | ::RELOAD_TYPE_RENDERTARGET))
	{
		renderer_AddPipelines(appState);
	}

	renderer_PrepareDescriptorSets(appState);

	return true;
}

void renderer_OnUnload(AppState* appState, ReloadDesc reloadDesc)
{
	assert(appState);
	assert(appState->renderer);

	::waitQueueIdle(appState->graphicsQueue);

	if (reloadDesc.mType & (::RELOAD_TYPE_SHADER | ::RELOAD_TYPE_RENDERTARGET))
	{
		renderer_RemovePipelines(appState);
	}

	if (reloadDesc.mType & (::RELOAD_TYPE_RESIZE | ::RELOAD_TYPE_RENDERTARGET))
	{
		renderer_RemoveSwapChain(appState);
		renderer_RemoveRenderTargets(appState);
	}

	if (reloadDesc.mType & ::RELOAD_TYPE_SHADER)
	{
		renderer_RemoveDescriptorSets(appState);
		renderer_RemoveShaders(appState);
	}
}

bool renderer_AddSwapChain(AppState* appState)
{
	SDL_PropertiesID properties = SDL_GetWindowProperties(appState->window);
	void* hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	::WindowHandle windowHandle = { ::WINDOW_HANDLE_TYPE_WIN32, (HWND)hwnd };

	int32_t width;
	int32_t height;
	SDL_GetWindowSizeInPixels(appState->window, &width, &height);

	::SwapChainDesc desc = {};
	desc.mWindowHandle = windowHandle;
	desc.mPresentQueueCount = 1;
	desc.ppPresentQueues = &appState->graphicsQueue;
	desc.mWidth = (uint32_t)width;
	desc.mHeight = (uint32_t)height;
	desc.mImageCount = ::getRecommendedSwapchainImageCount(appState->renderer, &windowHandle);
	desc.mColorFormat = ::getSupportedSwapchainFormat(appState->renderer, &desc, ::COLOR_SPACE_SDR_SRGB);
	desc.mColorSpace = ::COLOR_SPACE_SDR_SRGB;
	desc.mEnableVsync = true;
	desc.mFlags = ::SWAP_CHAIN_CREATION_FLAG_NONE;
	::addSwapChain(appState->renderer, &desc, &appState->swapChain);

	return appState->swapChain != NULL;
}

void renderer_RemoveSwapChain(AppState* appState)
{
	::removeSwapChain(appState->renderer, appState->swapChain);
}

bool renderer_AddRenderTargets(AppState* appState)
{
	int32_t window_width;
	int32_t window_height;
	SDL_GetWindowSizeInPixels(appState->window, &window_width, &window_height);

	// Add Depth Buffer
	{
		::RenderTargetDesc desc = {};
		desc.mArraySize = 1;
		desc.mClearValue.depth = 0.0f;
		desc.mClearValue.stencil = 0;
		desc.mDepth = 1;
		desc.mFormat = ::TinyImageFormat_D32_SFLOAT;
		desc.mStartState = ::RESOURCE_STATE_DEPTH_WRITE;
		desc.mWidth = (uint32_t)window_width;
		desc.mHeight = (uint32_t)window_height;
		desc.mSampleCount = ::SAMPLE_COUNT_1;
		desc.mSampleQuality = 0;
		desc.mFlags = ::TEXTURE_CREATION_FLAG_ON_TILE;
		::addRenderTarget(appState->renderer, &desc, &appState->depthBuffer);

		if (!appState->depthBuffer)
		{
			LOGF(eERROR, "Failed to create depth buffer");
			return false;
		}
	}

	return true;
}

void renderer_RemoveRenderTargets(AppState* appState)
{
	::removeRenderTarget(appState->renderer, appState->depthBuffer);
}

void renderer_Draw(AppState* appState)
{
	int32_t windowWidth;
	int32_t windowHeight;
	SDL_GetWindowSizeInPixels(appState->window, &windowWidth, &windowHeight);

	uint32_t swapChainImageIndex;
	::acquireNextImage(appState->renderer, appState->swapChain, appState->imageAcquiredSemaphore, NULL, &swapChainImageIndex);

	::RenderTarget* renderTarget = appState->swapChain->ppRenderTargets[swapChainImageIndex];
	::GpuCmdRingElement elem = ::getNextGpuCmdRingElement(&appState->graphicsCmdRing, true, 1);

	// Stall if CPU is running 2 frames ahead of GPU
	::FenceStatus fenceStatus;
	::getFenceStatus(appState->renderer, elem.pFence, &fenceStatus);
	if (fenceStatus == ::FENCE_STATUS_INCOMPLETE)
		::waitForFences(appState->renderer, 1, &elem.pFence);

	::resetCmdPool(appState->renderer, elem.pCmdPool);

	::Cmd* cmd = elem.pCmds[0];
	::beginCmd(cmd);

	::RenderTargetBarrier barriers[] = {
		{ renderTarget, ::RESOURCE_STATE_PRESENT, ::RESOURCE_STATE_RENDER_TARGET },
	};
	::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

	BindRenderTargetsDesc bindRenderTargets = {};
	bindRenderTargets.mRenderTargetCount = 1;
	bindRenderTargets.mRenderTargets[0] = {};
	bindRenderTargets.mRenderTargets[0].pRenderTarget = renderTarget;
	bindRenderTargets.mRenderTargets[0].mLoadAction = ::LOAD_ACTION_CLEAR;
	bindRenderTargets.mRenderTargets[0].mClearValue = { 0.3f, 0.3f, 0.3f, 1.0f };
	bindRenderTargets.mRenderTargets[0].mOverrideClearValue = true;
	::cmdBindRenderTargets(cmd, &bindRenderTargets);
	::cmdSetViewport(cmd, 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f);
	::cmdSetScissor(cmd, 0, 0, (uint32_t)windowWidth, (uint32_t)windowHeight);

	barriers[0] = { renderTarget, ::RESOURCE_STATE_RENDER_TARGET, ::RESOURCE_STATE_PRESENT };
	::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

	::endCmd(cmd);

	::FlushResourceUpdateDesc flushUpdateDesc = {};
	flushUpdateDesc.mNodeIndex = 0;
	::flushResourceUpdates(&flushUpdateDesc);
	::Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, appState->imageAcquiredSemaphore };

	::QueueSubmitDesc submitDesc = {};
	submitDesc.mCmdCount = 1;
	submitDesc.mSignalSemaphoreCount = 1;
	submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
	submitDesc.ppCmds = &cmd;
	submitDesc.ppSignalSemaphores = &elem.pSemaphore;
	submitDesc.ppWaitSemaphores = waitSemaphores;
	submitDesc.pSignalFence = elem.pFence;
	::queueSubmit(appState->graphicsQueue, &submitDesc);

	::QueuePresentDesc presentDesc = {};
	presentDesc.mIndex = (uint8_t)swapChainImageIndex;
	presentDesc.mWaitSemaphoreCount = 1;
	presentDesc.pSwapChain = appState->swapChain;
	presentDesc.ppWaitSemaphores = &elem.pSemaphore;
	presentDesc.mSubmitDone = true;

	::queuePresent(appState->graphicsQueue, &presentDesc);
}

// TODO(gmodarelli): Use scratch vertex and index buffers and generate proper
// indices with mesh_optimizer
void renderer_LoadGeometry(RendererGeometry* geometry, const char* path)
{
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
		assert(obj->face_vertices[i] == 3);

		for (uint32_t j = 0; j < obj->face_vertices[i]; ++j)
		{
			fastObjIndex gi = obj->indices[indexOffset + j];

			MeshVertex* v = &geometry->vertices[geometry->numVertices + vertexOffset++];
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
			v->uv.y = obj->texcoords[gi.t * 2 + 1];
		}

		indexOffset += obj->face_vertices[i];
	}

	assert(vertexOffset == indexCount);
	geometry->numVertices += indexCount;

	for (uint32_t i = 0; i < indexCount; ++i) {
		geometry->indices[geometry->numIndices + i] = (uint32_t)i;
	}
	geometry->numIndices += indexCount;

	fast_obj_destroy(obj);
	return;
}

void renderer_AddShaders(AppState* appState)
{
	{
		ShaderLoadDesc uberShader = {};
		uberShader.mVert.pFileName = "Uber.vert";
		uberShader.mFrag.pFileName = "Uber.pixel";
		::addShader(appState->renderer, &uberShader, &appState->uberShader);
	}
}

void renderer_RemoveShaders(AppState* appState)
{
	::removeShader(appState->renderer, appState->uberShader);
}

void renderer_AddDescriptorSets(AppState* appState)
{
	::DescriptorSetDesc desc = {};
	desc.mNodeIndex = ROOT_PARAM_PerFrame;
	desc.mMaxSets = appState->dataBufferCount;
	desc.mNodeIndex = 0;
	desc.mDescriptorCount = 1;
	desc.pDescriptors = SRT_UberShaderData::PerFramePtr();
	::addDescriptorSet(appState->renderer, &desc, &appState->uniformDescriptorSet);
}

void renderer_PrepareDescriptorSets(AppState* appState){}

void renderer_RemoveDescriptorSets(AppState* appState)
{
	::removeDescriptorSet(appState->renderer, appState->uniformDescriptorSet);
}

void renderer_AddPipelines(AppState* appState)
{
	::RasterizerStateDesc rasterizerStateDesc = {};
	rasterizerStateDesc.mCullMode = ::CULL_MODE_BACK;
	rasterizerStateDesc.mFrontFace = ::FRONT_FACE_CCW;

	::DepthStateDesc depthStateDesc = {};
	depthStateDesc.mDepthTest = true;
	depthStateDesc.mDepthWrite = true;
	depthStateDesc.mDepthFunc = ::CMP_GEQUAL;

	// TODO: Get rid of vertex layouts and use vertex fetching instead
	::VertexLayout vertexLayout = {};
	{
		vertexLayout.mAttribCount = 4;
		vertexLayout.mBindingCount = 1;
		vertexLayout.mBindings[0].mRate = ::VERTEX_BINDING_RATE_VERTEX;
		vertexLayout.mBindings[0].mStride = sizeof(MeshVertex);

		vertexLayout.mAttribs[0] = {};
		vertexLayout.mAttribs[0].mSemantic = ::SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = ::TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mBinding = 0;

		vertexLayout.mAttribs[1] = {};
		vertexLayout.mAttribs[1].mSemantic = ::SEMANTIC_NORMAL;
		vertexLayout.mAttribs[1].mFormat = ::TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[1].mOffset = 12;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mBinding = 0;

		vertexLayout.mAttribs[2] = {};
		vertexLayout.mAttribs[2].mSemantic = ::SEMANTIC_COLOR;
		vertexLayout.mAttribs[2].mFormat = ::TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[2].mOffset = 24;
		vertexLayout.mAttribs[2].mLocation = 2;
		vertexLayout.mAttribs[2].mBinding = 0;

		vertexLayout.mAttribs[3] = {};
		vertexLayout.mAttribs[3].mSemantic = ::SEMANTIC_TEXCOORD0;
		vertexLayout.mAttribs[3].mFormat = ::TinyImageFormat_R32G32_SFLOAT;
		vertexLayout.mAttribs[3].mOffset = 36;
		vertexLayout.mAttribs[3].mLocation = 3;
		vertexLayout.mAttribs[3].mBinding = 0;
	}

	::PipelineDesc desc = {};
	desc.mType = ::PIPELINE_TYPE_GRAPHICS;
	::GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
	pipelineSettings.mPrimitiveTopo = ::PRIMITIVE_TOPO_TRI_LIST;
	pipelineSettings.mRenderTargetCount = 1;
	pipelineSettings.pDepthState = &depthStateDesc;
	pipelineSettings.pColorFormats = &appState->swapChain->ppRenderTargets[0]->mFormat;
	pipelineSettings.mSampleCount = appState->swapChain->ppRenderTargets[0]->mSampleCount;
	pipelineSettings.mSampleQuality = appState->swapChain->ppRenderTargets[0]->mSampleQuality;
	pipelineSettings.mDepthStencilFormat = appState->depthBuffer->mFormat;
	pipelineSettings.pShaderProgram = appState->uberShader;
	pipelineSettings.pVertexLayout = &vertexLayout;
	pipelineSettings.pRasterizerState = &rasterizerStateDesc;
	pipelineSettings.mVRFoveatedRendering = false;
	::addPipeline(appState->renderer, &desc, &appState->uberPipeline);
}

void renderer_RemovePipelines(AppState* appState)
{
	::removePipeline(appState->renderer, appState->uberPipeline);
}