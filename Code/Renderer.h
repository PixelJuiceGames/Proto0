#pragma once

#include "Scene.h"
#include <OS/Interfaces/IOperatingSystem.h>

namespace renderer
{
	bool Initialize(void* nativeWindowHandle);
	void Exit();
	bool OnLoad(::ReloadDesc reloadDesc);
	void OnUnload(::ReloadDesc reloadDesc);

	void Draw(const Scene* scene);
}
