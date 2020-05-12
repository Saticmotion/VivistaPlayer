#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Manager.h"

#include <assert.h>
#include <math.h>
#include <vector>
#include <thread>
#include <string>
#include <memory>
#include <list>

using namespace std;

typedef struct VideoContext
{
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

bool getVideoContext(int id, shared_ptr<VideoContext>& videoCtx) 
{
	for (VideoContextIter it = videoContexts.begin(); it != videoContexts.end(); it++) {
		if ((*it)->id == id) {
			videoCtx = *it;
			return true;
		}
	}

	return false;
}

void removeVideoContext(int id)
{
	for (VideoContextIter it = videoContexts.begin(); it != videoContexts.end(); it++)
	{
		if ((*it)->id == id) {
			videoContexts.erase(it);
			return;
		}
	}
}

static void RenderVideo(int id)
{
	void* textureHandle = g_TextureHandle;
	int width = g_TextureWidth;
	int height = g_TextureHeight;
	if (!textureHandle)
		return;

	int textureRowPitch;

	shared_ptr<VideoContext> localVideoContext;
	if (getVideoContext(id, localVideoContext)) 
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

	RenderVideo(eventID);
}

// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}

#pragma region Player

// TODO initdecoder
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API InitDecoder(const char* path, int& id)
{
	int newID = 0;
	shared_ptr<VideoContext> videoCtx;
	while (getVideoContext(newID, videoCtx)) 
	{
		newID++; 
	}

	videoCtx = make_shared<VideoContext>();
	videoCtx->manager = make_shared<VideoContext>();
	videoCtx->id = newID;
	id = videoCtx->id;
	videoCtx->path = string(path);
	videoCtx->isContentReady = false;

	videoCtx->initThread = thread([videoCtx]() {
		videoCtx->manager->Init(videoCtx->path.c_str());
	});

	videoContexts.push_back(videoCtx);

	return 0;
}

// GetPlayerState, get the current state of the player
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetPlayerState(int id) 
{
	shared_ptr<VideoContext> videoCtx;

	if (!getVideoContext(id, videoCtx) || videoCtx->manager == NULL) { 
		return -1;
	}

	return videoCtx->manager->GetPlayerState();
}

// --------------------------------------------------------------------------
// CreateTexture, create a new texture resource on the supported graphic device
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API CreateTexture(int id, void*& tex0, void*& tex1, void*& tex2)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) 
	{
		return;
	}
}

// TODO Start playing
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Start(int id)
{
	shared_ptr<VideoContext> videoCtx;

	if (!getVideoContext(id, videoCtx) || videoCtx->manager == NULL)
	{ 
		return false; 
	}

	if (videoCtx->initThread.joinable()) {
		videoCtx->initThread.join();
	}

	auto localManager = videoCtx->manager;
	if (localManager->GetPlayerState() >= Manager::PlayerState::INITIALIZED)
	{
		localManager->Start();
	}

	if (!localManager->getVideoInfo().isEnabled) {
		videoCtx->isContentReady = true;
	}

	return true;
}

// TODO destroy
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Destroy(int id)
{
	shared_ptr<VideoContext> videoCtx;

	if (!getVideoContext(id, videoCtx)) { 
		return;
	}

	if (videoCtx->initThread.joinable()) {
		videoCtx->initThread.join();
	}

	videoCtx->manager = NULL;
	videoCtx->path.clear();
	videoCtx->progressTime = 0.0f;
	videoCtx->lastUpdateTime = 0.0f;
	videoCtx->isContentReady = false;

	removeVideoContext(videoCtx->id);
	videoCtx->id = -1;
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsEOF(int id) 
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx) || videoCtx->manager == NULL)
	{
		return true;
	}

	return videoCtx->manager->GetPlayerState() == Manager::PlayerState::PLAY_EOF;
}

#pragma endregion


#pragma region Video

// TODO is enabled.
bool nativeIsVideoEnabled(int id)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx))
	{ 
		return false;
	}

	if (videoCtx->manager->GetPlayerState() < Manager::PlayerState::INITIALIZED)
	{
		return false;
	}

	bool ret = videoCtx->manager->getVideoInfo().isEnabled;
	return ret;
}


extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API EnableVideo(int id, bool isEnabled)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx))
	{
		return;
	}

	videoCtx->manager->EnableVideo(isEnabled);
}

// TODO videoformat
void nativeGetVideoFormat(int id, int& width, int& height, float& totalTime)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx))
	{ 
		return; 
	}

	if (videoCtx->manager->GetPlayerState() < Manager::PlayerState::INITIALIZED)
	{
		return;
	}

	Decoder::VideoInfo* videoInfo = &(videoCtx->manager->getVideoInfo());
	width = videoInfo->width;
	height = videoInfo->height;
	totalTime = (float)(videoInfo->totalTime);
}

// TODO videotime
void SetVideoTime(int id, float currentTime)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) { 
		return;
	}

	videoCtx->progressTime = currentTime;
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsContentReady(int id)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) 
	{ 
		return false;
	}

	return videoCtx->isContentReady;
}

// TODO bufferfull
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsVideoBufferFull(int id)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx)) 
	{ 
		return false;
	}

	return videoCtx->manager->isVideoBufferFull();
}

// TODO videobufferempty
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsVideoBufferEmpty(int id)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx))
	{
		return false;
	}

	return videoCtx->manager->isVideoBufferEmpty();
}

#pragma endregion

#pragma region Audio

// TODO is enabled

//extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API EnableAudio(int id, bool isEnabled)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx))
//	{
//		return;
//	}
//
//	videoCtx->manager->EnableAudio(isEnabled);
//}

// TODO audiochannels

// TODO audioformat

// TODO audiodata

// free audio

#pragma endregion

#pragma region Seeking

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Seek(int id, float seconds)
{
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx))
	{ 
		return;
	}

	if (videoCtx->manager->GetPlayerState() < Manager::PlayerState::INITIALIZED)
	{
		return;
	}

	videoCtx->manager->Seek(seconds);

	if (!videoCtx->manager->getVideoInfo().isEnabled)
	{
		videoCtx->isContentReady = true;
	}
	else 
{
		videoCtx->isContentReady = false;
	}
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsSeekOver(int id) {
	shared_ptr<VideoContext> videoCtx;
	if (!getVideoContext(id, videoCtx))
	{ 
		return false;
	}

	return !(videoCtx->manager->GetPlayerState() == Manager::PlayerState::SEEK);
}

#pragma endregion