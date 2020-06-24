#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Manager.h"
#include "Logger.h"

#include <cassert>
#include <cmath>
#include <vector>
#include <thread>
#include <string>
#include <memory>
#include <list>

using namespace std;

typedef struct VideoContext
{
	string path;
	thread initThread;
	Manager* manager = NULL;
	float progressTime = 0.0f;
	float lastUpdateTime = -1.0f;
	bool isContentReady = false;
} VideoContext;

typedef void(__stdcall* DebugCallback) (const char* str);
DebugCallback DebugLogCallback;

VideoContext* videoContext;

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;
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

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
	//NOTE(Simon): No further cleanup required; happens only on program exit
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RegisterDebugLogCallback(DebugCallback callback)
{
	if (callback)
	{
		DebugLogCallback = callback;
	}
}

void DebugInUnity(char* message)
{
	if (DebugLogCallback)
	{
		DebugLogCallback(message);
	}
}

//NOTE(Simon): This callback's signature needs the ID parameter
static void UNITY_INTERFACE_API Update(int ID)
{
	if (s_CurrentAPI == NULL)
		return;

	Manager* localManager = videoContext->manager;

	if (localManager != NULL &&	localManager->GetPlayerState() >= Manager::PlayerState::INITIALIZED)
	{
		double videoDecCurTime = localManager->getVideoInfo().lastTime;

		if (videoDecCurTime <= videoContext->progressTime)
		{
			uint8_t* ptrY = NULL;
			uint8_t* ptrU = NULL;
			uint8_t* ptrV = NULL;
			double curFrameTime = localManager->GetVideoFrame(&ptrY, &ptrU, &ptrV);

			if (ptrY != NULL && curFrameTime != -1 && videoContext->lastUpdateTime != curFrameTime)
			{
				s_CurrentAPI->UploadYUVFrame(ptrY, ptrU, ptrV);
				videoContext->lastUpdateTime = (float)curFrameTime;
				videoContext->isContentReady = true;
			}
			localManager->FreeVideoFrame();
		}
	}
}

//NOTE(Simon): We need to call into the library's update through a callback. It gets called on a separate thread in Unity
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetUpdateFunc()
{
	return Update;
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeInitDecoder(const char* path, int& id)
{
	videoContext = new VideoContext();
	videoContext->manager = new Manager();
	videoContext->path = string(path);
	videoContext->isContentReady = false;

	videoContext->initThread = thread([]{
		videoContext->manager->Init(videoContext->path.c_str());
	});

	return 0;
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeGetPlayerState()
{
	if (videoContext->manager == NULL)
	{
		return -1;
	}

	return videoContext->manager->GetPlayerState();
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeCreateTexture(void** texY, void** texU, void** texV)
{
	if (texY == nullptr || texU == nullptr || texV == nullptr)
	{
		return false;
	}

	unsigned int width = videoContext->manager->getVideoInfo().width;
	unsigned int height = videoContext->manager->getVideoInfo().height;
	s_CurrentAPI->Create(width, height, texY, texU, texV);
	return true;
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeStart()
{
	if (videoContext->manager == NULL)
	{
		return false;
	}

	if (videoContext->initThread.joinable())
	{
		videoContext->initThread.join();
	}

	Manager* localManager = videoContext->manager;
	if (localManager->GetPlayerState() >= Manager::PlayerState::INITIALIZED)
	{
		localManager->Start();
	}

	if (!localManager->getVideoInfo().isEnabled)
	{
		videoContext->isContentReady = true;
	}

	return true;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeDestroy()
{
	videoContext->manager->Stop();
	if (videoContext->initThread.joinable())
	{
		videoContext->initThread.join();
	}

	videoContext->manager = NULL;
	videoContext->path.clear();
	videoContext->progressTime = 0.0f;
	videoContext->lastUpdateTime = 0.0f;
	videoContext->isContentReady = false;
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeIsEOF(int id)
{
	if (videoContext->manager == NULL)
	{
		return true;
	}

	return videoContext->manager->GetPlayerState() == Manager::PlayerState::PLAY_EOF;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity(float time)
{
	videoContext->progressTime = time;
}

extern "C" Decoder::VideoInfo UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API NativeGetVideoInfo()
{
	return videoContext->manager->getVideoInfo();
}

#pragma region Video

// TODO is enabled.
//bool nativeIsVideoEnabled(int id)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx))
//	{ 
//		return false;
//	}
//
//	if (videoCtx->manager->GetPlayerState() < Manager::PlayerState::INITIALIZED)
//	{
//		return false;
//	}
//
//	bool ret = videoCtx->manager->getVideoInfo().isEnabled;
//	return ret;
//}


//extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API EnableVideo(int id, bool isEnabled)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx))
//	{
//		return;
//	}
//
//	videoCtx->manager->EnableVideo(isEnabled);
//}

// TODO videotime
//void SetVideoTime(int id, float currentTime)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx)) { 
//		return;
//	}
//
//	videoCtx->progressTime = currentTime;
//}

//extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsContentReady(int id)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx)) 
//	{ 
//		return false;
//	}
//
//	return videoCtx->isContentReady;
//}

// TODO bufferfull
//extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsVideoBufferFull(int id)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx)) 
//	{ 
//		return false;
//	}
//
//	return videoCtx->manager->isVideoBufferFull();
//}

// TODO videobufferempty
//extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsVideoBufferEmpty(int id)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx))
//	{
//		return false;
//	}
//
//	return videoCtx->manager->isVideoBufferEmpty();
//}

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

//extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Seek(int id, float seconds)
//{
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx))
//	{ 
//		return;
//	}
//
//	if (videoCtx->manager->GetPlayerState() < Manager::PlayerState::INITIALIZED)
//	{
//		return;
//	}
//
//	videoCtx->manager->Seek(seconds);
//
//	if (!videoCtx->manager->getVideoInfo().isEnabled)
//	{
//		videoCtx->isContentReady = true;
//	}
//	else 
//{
//		videoCtx->isContentReady = false;
//	}
//}
//
//extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API IsSeekOver(int id) {
//	shared_ptr<VideoContext> videoCtx;
//	if (!getVideoContext(id, videoCtx))
//	{ 
//		return false;
//	}
//
//	return !(videoCtx->manager->GetPlayerState() == Manager::PlayerState::SEEK);
//}

#pragma endregion