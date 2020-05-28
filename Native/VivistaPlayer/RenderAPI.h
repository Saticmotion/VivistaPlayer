#pragma once

#include "Unity/IUnityGraphics.h"

#include <stddef.h>

struct IUnityInterface;

class RenderAPI
{
public:
	//virtual	~RenderAPI();
	// Process general event like initialization, shutdown, device loss/reset etc.
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) = 0;
	// Is the API using "reversed" (1.0 at near plane, 0.0 at far plane) depth buffer?
	// Reversed Z is used on modern platforms, and improves depth buffer precision.
	virtual bool GetUsesReverseZ() = 0;

	// Create a new texture on the GPU You need to pass texture width/height too, since some graphics APIs
	// (e.g. OpenGL ES) do not have a good way to query that from the texture itself...
	//
	// Returns pointer into the data buffer to write into (or NULL on failure), and pitch in bytes of a single texture row.
	virtual void Create(int textureWidth, int textureHeight) = 0;
	// Upload new texture data to an existing texture resource.
	virtual void Upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) = 0;
	// Get pointers to texture resources.
	virtual void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv);
};

RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType);