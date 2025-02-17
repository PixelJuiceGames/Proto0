// SDL3

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// The-Forge

#include <Utilities/Interfaces/IFileSystem.h>
#include <Utilities/Interfaces/ILog.h>
#include <Utilities/Interfaces/IMemory.h>
#include <Graphics/Interfaces/IGraphics.h>

static SDL_Window *window = NULL;
static Renderer *renderer = NULL;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
	window = SDL_CreateWindow("Prototype 0", 1920, 1080, SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	// Initialize Memory Allocation System
	{
		if (!initMemAlloc("Prototype 0"))
		{
			SDL_Log("Couldn't initialize The-Forge Memory Allocation system");
			return SDL_APP_FAILURE;
		}
	}

	// Initialize The-Forge File System
	{
		FileSystemInitDesc desc = FileSystemInitDesc{};
		desc.pAppName = "Prototype 0";
		if (!initFileSystem(&desc))
		{
			SDL_Log("Couldn't initialize The-Forge File system");
			return SDL_APP_FAILURE;
		}
	}

	// Initialize The-Forge Logging System
	{
		initLog("Prototype 0", LogLevel::eWARNING);
	}

	//// Initialize The-Forge Renderer
	{
		initGPUConfiguration(NULL);

		RendererDesc desc = RendererDesc{};
		initRenderer("Prototype 0", &desc, &renderer);
		if (!renderer)
		{
			SDL_Log("Couldn't initialize The-Forge Renderer");
			return SDL_APP_FAILURE;
		}
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
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	exitRenderer(renderer);
	exitGPUConfiguration();
	exitLog();
	exitFileSystem();
	exitMemAlloc();
}