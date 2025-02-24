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
	float movementSpeed;
	::float2 movementVector;
};

struct Scene
{
	Player player;
	PlayerCamera playerCamera;
};