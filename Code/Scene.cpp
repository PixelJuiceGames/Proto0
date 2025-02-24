#include "Scene.h"

::float3 PlayerCamera::getViewDir()
{
	::mat4 transposed = ::transpose(viewMatrix);
	::Vector4 forward = -transposed.getCol2();
	return { forward.getX(), forward.getY(), forward.getZ() };
}


void PlayerCamera::updateViewMatrix()
{
	viewMatrix = ::mat4::lookAtRH({ position.x, position.y, position.z }, 
		{ lookAt.x, lookAt.y, lookAt.z }, { 0.0f, 0.0f, 1.0f });
}