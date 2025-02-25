#include <assert.h>

// SDL3

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// Math
#include <Utilities/Math/MathTypes.h>

#include "Renderer.h"
#include "Scene.h"

struct Timer
{
	uint64_t lastTicks = 0;
	float deltaTime = 0.0f;

	void Tick()
	{
		uint64_t ticks = SDL_GetTicksNS();
		uint64_t deltaNS = ticks - lastTicks;
		deltaTime = (float)(deltaNS * 1e-9);
		lastTicks = ticks;
	}
};

struct AppState
{
	SDL_Window* window = NULL;

	Timer timer;
	Scene scene;
};

void game_UpdatePlayerMovement(AppState* appState);

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	AppState* as = (AppState*)SDL_calloc(1, sizeof(AppState));
	if (!as)
	{
		return SDL_APP_FAILURE;
	}

	memset(as, 0, sizeof(AppState));

	*appstate = as;

	// Initialize Scene
	{
		// Player
		as->scene.player.position = { 0.0f, 0.0f, 0.0f };
		as->scene.player.scale = { 0.25f, 0.25f, 2.0f };
		as->scene.player.movementSpeed = 2.0f;
		as->scene.player.movementVector = { 0.0f, 0.0f };
		
		// Player Camera
		as->scene.playerCamera.position = as->scene.player.position + ::float3{ 0.0f, -10.0f, 10.0f };
		as->scene.playerCamera.lookAt = { 0.0f, 0.0f, 0.0f };
		as->scene.playerCamera.updateViewMatrix();

		// Player Light
		as->scene.playerLight.type = LightType::PointLight;
		as->scene.playerLight.position = as->scene.player.position + ::float3{ 0.0f, -1.0f, 5.0f };
		as->scene.playerLight.color = { 1.0f, 1.0f, 1.0f };
		as->scene.playerLight.intensity = 10.0f;
		as->scene.playerLight.range = 10.0f;

		// Ground
		as->scene.entities = (Entity*)SDL_calloc(1024, sizeof(Entity));
		as->scene.entityCount = 0;
		for (int32_t y = -10; y < 10; y++)
		{
			for (int32_t x = -10; x < 10; x++)
			{
				Entity& entity = as->scene.entities[as->scene.entityCount++];
				entity.position = { x + 0.5f, y + 0.5f, 0.0f };
				entity.scale = { 1.0f, 1.0f, 1.0f };
				entity.meshHandle = 0; // plane
				entity.materialHandle = 1; // grid debug material
			}
		}
	}

	as->window = SDL_CreateWindow("Prototype 0", 1920, 1080, SDL_WINDOW_RESIZABLE);
	if (!as->window)
	{
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_PropertiesID properties = SDL_GetWindowProperties(as->window);
	void* hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	if (!renderer::Initialize(hwnd))
	{
		return SDL_APP_FAILURE;
	}

	renderer::LoadScene(&as->scene);
	
	SDL_Log("Initialized");
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

	AppState* as = (AppState*)appstate;

	if (event->type == SDL_EVENT_WINDOW_RESIZED)
	{
		renderer::OnUnload({ ::RELOAD_TYPE_RESIZE });
		renderer::OnLoad({ ::RELOAD_TYPE_RESIZE });
	}

	if (event->type == SDL_EVENT_KEY_DOWN)
	{
		if (event->key.key == SDLK_R)
		{
			renderer::OnUnload({ ::RELOAD_TYPE_SHADER });
			renderer::OnLoad({ ::RELOAD_TYPE_SHADER });
		}

		if (event->key.key == SDLK_A)
		{
			as->scene.player.movementVector.x = -1.0f;
		}
		if (event->key.key == SDLK_D)
		{
			as->scene.player.movementVector.x = 1.0f;
		}
		if (event->key.key == SDLK_W)
		{
			as->scene.player.movementVector.y = 1.0f;
		}
		if (event->key.key == SDLK_S)
		{
			as->scene.player.movementVector.y = -1.0f;
		}
	}

	if (event->type == SDL_EVENT_KEY_UP)
	{
		if (event->key.key == SDLK_A)
		{
			if (as->scene.player.movementVector.x < 0)
				as->scene.player.movementVector.x = 0.0f;
		}
		if (event->key.key == SDLK_D)
		{
			if (as->scene.player.movementVector.x > 0)
				as->scene.player.movementVector.x = 0.0f;
		}
		if (event->key.key == SDLK_W)
		{
			if (as->scene.player.movementVector.y > 0)
				as->scene.player.movementVector.y = 0.0f;
		}
		if (event->key.key == SDLK_S)
		{
			if (as->scene.player.movementVector.y < 0)
				as->scene.player.movementVector.y = 0.0f;
		}
	}

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	AppState* as = (AppState*)appstate;
	as->timer.Tick();

	game_UpdatePlayerMovement(as);

	renderer::Draw(&as->scene);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	if (appstate != NULL)
	{
		AppState* as = (AppState*)appstate;
		renderer::Exit();

		SDL_free(as);
	}
}

void game_UpdatePlayerMovement(AppState* appState)
{
	if (appState->scene.player.movementVector.x != 0 || appState->scene.player.movementVector.y != 0)
	{
		appState->scene.player.movementVector = ::normalize(appState->scene.player.movementVector);
	}

	::float2 positionOffset = {
		appState->scene.player.movementVector.x * appState->scene.player.movementSpeed * appState->timer.deltaTime,
		appState->scene.player.movementVector.y * appState->scene.player.movementSpeed * appState->timer.deltaTime
	};

	appState->scene.player.position.x += positionOffset.x;
	appState->scene.player.position.y += positionOffset.y;
	appState->scene.player.position.z = appState->scene.player.position.z;

	appState->scene.playerCamera.position.x += positionOffset.x;
	appState->scene.playerCamera.position.y += positionOffset.y;
	appState->scene.playerCamera.lookAt = appState->scene.player.position;
	appState->scene.playerCamera.updateViewMatrix();

	appState->scene.playerLight.position.x += positionOffset.x;
	appState->scene.playerLight.position.y += positionOffset.y;
}

