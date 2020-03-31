#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Manager.h"

#include <assert.h>
#include <math.h>
#include <vector>
#include <thread>
#include <string>
#include <memory>

using namespace std;

typedef struct VideoContext {
	int id = -1;
	string path = "";
	thread initThread;
	shared_ptr<Manager> manager = NULL;
	float progressTime = 0.0f;
	float lastUpdateTime = -1.0f;
	bool isContentReady = false;
} VideoContext;

list<shared_ptr<VideoContext>> videoContexts;
typedef list<shared_ptr<VideoContext>>::iterator VideoContextIter;

// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.

static void* g_TextureHandle = NULL;
static int   g_TextureWidth = 0;
static int   g_TextureHeight = 0;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTextureFromUnity(void* textureHandle, int w, int h)
{
	// A script calls this at initialization time; just remember the texture pointer here.
	// Will update texture pixels each frame from the plugin rendering event (texture update
	// needs to happen on the rendering thread).
	g_TextureHandle = textureHandle;
	g_TextureWidth = w;
	g_TextureHeight = h;
}

// --------------------------------------------------------------------------
// UnitySetInterfaces

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

#if SUPPORT_VULKAN
	if (s_Graphics->GetRenderer() == kUnityGfxRendererNull)
	{
		extern void RenderAPI_Vulkan_OnPluginLoad(IUnityInterfaces*);
		RenderAPI_Vulkan_OnPluginLoad(unityInterfaces);
	}
#endif // SUPPORT_VULKAN

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

#if UNITY_WEBGL
typedef void	(UNITY_INTERFACE_API* PluginLoadFunc)(IUnityInterfaces* unityInterfaces);
typedef void	(UNITY_INTERFACE_API* PluginUnloadFunc)();

extern "C" void	UnityRegisterRenderingPlugin(PluginLoadFunc loadPlugin, PluginUnloadFunc unloadPlugin);

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RegisterPlugin()
{
	UnityRegisterRenderingPlugin(UnityPluginLoad, UnityPluginUnload);
}
#endif

// --------------------------------------------------------------------------
// GraphicsDeviceEvent


static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	// Create graphics API implementation upon initialization
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		assert(s_CurrentAPI == NULL);
		s_DeviceType = s_Graphics->GetRenderer();
		s_CurrentAPI = CreateRenderAPI(s_DeviceType);
	}

	// Let the implementation process the device related events
	if (s_CurrentAPI)
	{
		s_CurrentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
	}

	// Cleanup graphics API implementation upon shutdown
	if (eventType == kUnityGfxDeviceEventShutdown)
	{
		delete s_CurrentAPI;
		s_CurrentAPI = NULL;
		s_DeviceType = kUnityGfxRendererNull;
	}
}

// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.

bool getVideoContext(int id, shared_ptr<VideoContext>& videoCtx) {
	for (VideoContextIter it = videoContexts.begin(); it != videoContexts.end(); it++) {
		if ((*it)->id == id) {
			videoCtx = *it;
			return true;
		}
	}

	return false;
}

void removeVideoContext(int id) {
	for (VideoContextIter it = videoContexts.begin(); it != videoContexts.end(); it++) {
		if ((*it)->id == id) {
			videoContexts.erase(it);
			return;
		}
	}
}

static void RenderVideo()
{
	void* textureHandle = g_TextureHandle;
	int width = g_TextureWidth;
	int height = g_TextureHeight;
	if (!textureHandle)
		return;

	int textureRowPitch;

	shared_ptr<VideoContext> localVideoContext;
	if (getVideoContext(localVideoContext)) 
	{
		Manager* localManager = localVideoContext->manager.get();

		// TODO videoinfo
		if (localManager != NULL &&
			localManager->GetPlayerState() >= Manager::PlayerState::INITIALIZED)
		{
			s_CurrentAPI->Create(width, height, &textureRowPitch);

			uint8_t* ptrY = NULL;
			uint8_t* ptrU = NULL;
			uint8_t* ptrV = NULL;
			double currentFrameTime = localManager->GetVideoFrame(&ptrY, &ptrU, &ptrV);
			if (ptrY != NULL && currentFrameTime != -1 && localVideoContext->lastUpdateTime != currentFrameTime)
			{
				s_CurrentAPI->Upload(ptrY, ptrU, ptrV);
				localVideoContext->lastUpdateTime = (float)currentFrameTime;
				localVideoContext->isContentReady = true;
			}
			localManager->FreeVideoFrame();
		}
	}
}

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_CurrentAPI == NULL)
		return;

	RenderVideo();
}

// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API InitDecoder(const char* path)
{
	shared_ptr<VideoContext> videoCtx;

	videoCtx = make_shared<VideoContext>();
	videoCtx->path = string(path);
	videoCtx->isContentReady = false;

	videoCtx->initThread = thread([videoCtx]() {
		videoCtx->manager->Init(videoCtx->path.c_str());
	});
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetPlayerState()
{
	shared_ptr<VideoContext> videoCtx;

	if (!getVideoContext(id, videoCtx) || videoCtx->manager == NULL) { return -1; }

	return videoCtx->manager->GetPlayerState();
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Start()
{
	shared_ptr<VideoContext> videoCtx;

	if (!getVideoContext(id, videoCtx) || videoCtx->manager == NULL) { return -1; }


	if (videoCtx->initThread.joinable()) {
		videoCtx->initThread.join();
	}

	auto localManager = videoCtx->manager;
	if (localManager->GetPlayerState >= Manager::PlayerState::INITIALIZED)
	{
		localManager->Start();
	}

	// TODO

	return true;
}



extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API EnableVideo(bool state)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) { return; }

	videoCtx->manager->EnableVideo = state;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API EnableAudio(bool state)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) { return; }

	videoCtx->manager->EnableAudio(state);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Seek(float sec)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) { return; }

	videoCtx->manager->Seek(sec);
}

bool nativeIsEOF(int id) {
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx) || videoCtx->manager == NULL) { return true; }

	return videoCtx->manager->GetPlayerState() == Manager::PlayerState::PLAY_EOF;
}