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

#include <assert.h>

struct MeshVertex
{
	float3 position;
	float3 normal;
	float3 color;
	float2 uv;
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

	Renderer* renderer = NULL;
	Queue* graphicsQueue = NULL;
	GpuCmdRing graphicsCmdRing = {};
	SwapChain* swapChain = NULL;
	Semaphore* imageAcquiredSemaphore = NULL;
	uint32_t dataBufferCount = 2;

	RendererGeometry geometry = {};
};

bool renderer_Initialize(AppState* appState);
void renderer_Exit(AppState* appState);
bool renderer_OnLoad(AppState* appState, ReloadDesc reloadDesc);
void renderer_OnUnload(AppState* appState, ReloadDesc reloadDesc);
bool renderer_AddSwapChain(AppState* appState);
void renderer_RemoveSwapChain(AppState* appState);
void renderer_Draw(AppState* appState);
void renderer_LoadGeometry(RendererGeometry* geometry, const char* path);

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
		if (!initMemAlloc("Prototype 0"))
		{
			SDL_Log("Couldn't initialize The-Forge Memory Allocation system");
			return false;
		}
	}

	// Initialize The-Forge File System
	{
		FileSystemInitDesc desc = FileSystemInitDesc{};
		desc.pAppName = "Prototype 0";
		if (!initFileSystem(&desc))
		{
			SDL_Log("Couldn't initialize The-Forge File system");
			return false;
		}
	}

	// Initialize The-Forge Logging System
	{
		::initLog("Prototype 0", LogLevel::eWARNING);
	}

	// Initialize The-Forge Renderer
	{
		RendererDesc desc = RendererDesc{};
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
		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		::initQueue(appState->renderer, &queueDesc, &appState->graphicsQueue);

		GpuCmdRingDesc cmdRingDesc = {};
		cmdRingDesc.pQueue = appState->graphicsQueue;
		cmdRingDesc.mPoolCount = appState->dataBufferCount;
		cmdRingDesc.mCmdPerPoolCount = 1;
		cmdRingDesc.mAddSyncPrimitives = true;
		::initGpuCmdRing(appState->renderer, &cmdRingDesc, &appState->graphicsCmdRing);

		::initSemaphore(appState->renderer, &appState->imageAcquiredSemaphore);
	}

	::initResourceLoaderInterface(appState->renderer);

	if (!renderer_OnLoad(appState, { RELOAD_TYPE_ALL }))
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

	renderer_OnUnload(appState, { RELOAD_TYPE_ALL });

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

bool renderer_OnLoad(AppState* appState, ReloadDesc reloadDesc)
{
	assert(appState);
	assert(appState->renderer);

	if (reloadDesc.mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
	{
		if (!renderer_AddSwapChain(appState))
		{
			return false;
		}
	}

	return true;
}

void renderer_OnUnload(AppState* appState, ReloadDesc reloadDesc)
{
	assert(appState);
	assert(appState->renderer);

	::waitQueueIdle(appState->graphicsQueue);

	if (reloadDesc.mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
	{
		renderer_RemoveSwapChain(appState);
	}
}

bool renderer_AddSwapChain(AppState* appState)
{
	SDL_PropertiesID properties = SDL_GetWindowProperties(appState->window);
	void* hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	WindowHandle windowHandle = { WINDOW_HANDLE_TYPE_WIN32, (HWND)hwnd };

	int32_t width;
	int32_t height;
	SDL_GetWindowSizeInPixels(appState->window, &width, &height);

	SwapChainDesc desc = {};
	desc.mWindowHandle = windowHandle;
	desc.mPresentQueueCount = 1;
	desc.ppPresentQueues = &appState->graphicsQueue;
	desc.mWidth = (uint32_t)width;
	desc.mHeight = (uint32_t)height;
	desc.mImageCount = getRecommendedSwapchainImageCount(appState->renderer, &windowHandle);
	desc.mColorFormat = getSupportedSwapchainFormat(appState->renderer, &desc, COLOR_SPACE_SDR_SRGB);
	desc.mColorSpace = COLOR_SPACE_SDR_SRGB;
	desc.mEnableVsync = true;
	desc.mFlags = SWAP_CHAIN_CREATION_FLAG_NONE;
	::addSwapChain(appState->renderer, &desc, &appState->swapChain);

	return appState->swapChain != NULL;
}

void renderer_RemoveSwapChain(AppState* appState)
{
	::removeSwapChain(appState->renderer, appState->swapChain);
}

void renderer_Draw(AppState* appState)
{
	int32_t windowWidth;
	int32_t windowHeight;
	SDL_GetWindowSizeInPixels(appState->window, &windowWidth, &windowHeight);

	uint32_t swapChainImageIndex;
	::acquireNextImage(appState->renderer, appState->swapChain, appState->imageAcquiredSemaphore, NULL, &swapChainImageIndex);

	RenderTarget* renderTarget = appState->swapChain->ppRenderTargets[swapChainImageIndex];
	GpuCmdRingElement elem = ::getNextGpuCmdRingElement(&appState->graphicsCmdRing, true, 1);

	// Stall if CPU is running 2 frames ahead of GPU
	FenceStatus fenceStatus;
	::getFenceStatus(appState->renderer, elem.pFence, &fenceStatus);
	if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		::waitForFences(appState->renderer, 1, &elem.pFence);

	::resetCmdPool(appState->renderer, elem.pCmdPool);

	Cmd* cmd = elem.pCmds[0];
	::beginCmd(cmd);

	RenderTargetBarrier barriers[] = {
		{ renderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET },
	};
	::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

	BindRenderTargetsDesc bindRenderTargets = {};
	bindRenderTargets.mRenderTargetCount = 1;
	bindRenderTargets.mRenderTargets[0] = {};
	bindRenderTargets.mRenderTargets[0].pRenderTarget = renderTarget;
	bindRenderTargets.mRenderTargets[0].mLoadAction = LOAD_ACTION_CLEAR;
	bindRenderTargets.mRenderTargets[0].mClearValue = { 0.3f, 0.3f, 0.3f, 1.0f };
	bindRenderTargets.mRenderTargets[0].mOverrideClearValue = true;
	::cmdBindRenderTargets(cmd, &bindRenderTargets);
	::cmdSetViewport(cmd, 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f);
	::cmdSetScissor(cmd, 0, 0, (uint32_t)windowWidth, (uint32_t)windowHeight);

	barriers[0] = { renderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
	::cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

	::endCmd(cmd);

	FlushResourceUpdateDesc flushUpdateDesc = {};
	flushUpdateDesc.mNodeIndex = 0;
	::flushResourceUpdates(&flushUpdateDesc);
	Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, appState->imageAcquiredSemaphore };

	QueueSubmitDesc submitDesc = {};
	submitDesc.mCmdCount = 1;
	submitDesc.mSignalSemaphoreCount = 1;
	submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
	submitDesc.ppCmds = &cmd;
	submitDesc.ppSignalSemaphores = &elem.pSemaphore;
	submitDesc.ppWaitSemaphores = waitSemaphores;
	submitDesc.pSignalFence = elem.pFence;
	::queueSubmit(appState->graphicsQueue, &submitDesc);

	QueuePresentDesc presentDesc = {};
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