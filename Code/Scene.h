#pragma once

// Math
#include <Utilities/Math/MathTypes.h>

struct PlayerCamera
{
	::mat4 viewMatrix;
	::float3 position;
	::float3 lookAt;

	::float3 getViewDir();
	void updateViewMatrix();
};

struct Player
{
	::float3 position;
	::float3 scale;
	float movementSpeed;
	::float2 movementVector;
};

enum class LightType
{
	PointLight
};

struct Light
{
	LightType type;
	::float3 position;
	::float3 color;
	float intensity;
	float range;
};

struct Entity
{
	::float3 position;
	::float3 scale;
	uint32_t meshHandle;		// TMP
	uint32_t materialHandle;	// TMP
};

struct Scene
{
	Player player;
	PlayerCamera playerCamera;
	Light playerLight;

	Light* lights = NULL;
	uint32_t lightCount = 0;

	Entity* entities = NULL;
	uint32_t entityCount = 0;
};