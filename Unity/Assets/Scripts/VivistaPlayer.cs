using UnityEngine;
using UnityEngine.Events;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine.Video;

public class Yield
{
	public static WaitForEndOfFrame endOfFrame = new WaitForEndOfFrame();
}

public enum BufferState
{
	Empty,
	Normal,
	Full
}

[StructLayout(LayoutKind.Sequential)]
public struct VideoInfo
{
	public bool isEnabled;
	public int width;
	public int height;
	public double lastTime;
	public double totalTime;
	public BufferState bufferState;
}

public class VivistaPlayer : MonoBehaviour
{
	// Native plugin rendering events are only called if a plugin is used
	// by some script. This means we have to DllImport at least
	// one function in some active script.
	// For this example, we'll call into plugin's SetTimeFromUnity
	// function and pass the current time so the plugin can animate.

	[DllImport("VivistaPlayer")]
	private static extern void SetTimeFromUnity(float t);

	[DllImport("VivistaPlayer")]
	private static extern void DestroyDecoderNative();

	[DllImport("VivistaPlayer")]
	private static extern void SetVideoDisabledNative(bool status);

	[DllImport("VivistaPlayer")]
	private static extern void SetAudioDisabledNative(bool status);

	[DllImport("VivistaPlayer")]
	private static extern IntPtr GetRenderEventFunc();

	[DllImport("VivistaPlayer")]
	private static extern bool NativeCreateTexture(ref IntPtr y, ref IntPtr u, ref IntPtr v);

	[DllImport("VivistaPlayer")]
	private static extern void NativeInitDecoder(string path, ref int id);

	[DllImport("VivistaPlayer")]
	private static extern int NativeGetPlayerState(int id);

	[DllImport("VivistaPlayer")]
	private static extern bool NativeStart();

	[DllImport("VivistaPlayer")]
	private static extern VideoInfo NativeGetVideoInfo();

	[DllImport("VivistaPlayer")]
	private static extern void RegisterDebugLogCallback(DebugLogCallback logCallback);
	private delegate void DebugLogCallback(string message);

#if !UNITY_EDITOR
	[DllImport ("__Internal")]
	private static extern void RegisterPlugin();
#endif

	public bool playOnAwake = false;
	public string url = null;
	public float playbackSpeed = 1.0f;

	public enum PlayerState
	{
		NotInitialized,
		INTIALIZING,
		INITIALIZED,
		PLAY,
		STOP,
		PAUSE,
		SEEK,
		BUFFERING,
		EOF
	}

	public UnityEvent prepareCompleted;
	public UnityEvent started;

	private int decoderId = -1;

	private UnityEvent loopPointReached;

	// Video
	private int videoWidth = -1;
	private int videoHeight = -1;
	private bool videoDisabled = false;

	// Audio
	private const int AUDIO_FRAME_SIZE = 192000;
	private const int AUDIO_BUFFER_SIZE = 2048;
	private const int SWAP_BUFFER_NUM = 4;
	private bool audioDisabled = false;

	private AudioSource[] audioSources = new AudioSource[SWAP_BUFFER_NUM];
	private List<float> audioBuffer;

	private float volume;

	// Player
	private PlayerState lastState = PlayerState.NotInitialized;
	private PlayerState playerState = PlayerState.NotInitialized;

	private Texture2D videoTexY;
	private Texture2D videoTexU;
	private Texture2D videoTexV;

	private IntPtr renderEventFunc;

	private void Start()
	{
#if !UNITY_EDITOR
		RegisterPlugin();
#endif
		//CreateTextureAndPassToPlugin();
		//yield return StartCoroutine("CallPluginAtEndOfFrames");
		renderEventFunc = GetRenderEventFunc();
		GL.IssuePluginEvent(renderEventFunc, 1);

		RegisterDebugLogCallback(DebugLog);
	}

	private void Awake()
	{
		if (playOnAwake)
		{
			DebugLog("play on awake");
			prepareCompleted.AddListener(StartDecoding);
			Prepare(url);
		}
	}

	private void Update()
	{
		switch (playerState)
		{
			case PlayerState.SEEK:
				break;
			case PlayerState.BUFFERING:
				break;
			case PlayerState.PLAY:
				break;
			case PlayerState.PAUSE:
				break;
			case PlayerState.STOP:
				break;
			case PlayerState.EOF:
				break;
		}
	}

	private void Prepare(string path)
	{
		StartCoroutine(InitDecoder(path));
	}

	private IEnumerator InitDecoder(string path)
	{
		DebugLog("init Decoder");
		playerState = PlayerState.INTIALIZING;

		url = path;
		decoderId = -1;
		NativeInitDecoder(path, ref decoderId);

		var videoInfo = NativeGetVideoInfo();
		videoWidth = videoInfo.width;
		videoHeight = videoInfo.height;

		CreateTextures();

		int result;
		do
		{
			yield return null;
			result = NativeGetPlayerState(decoderId);
		}
		while (result != 1 && result != -1);

		if (result == 1)
		{
			prepareCompleted.Invoke();
			DebugLog("Init success");
		}
		if (result == -1)
		{
			DebugLog("Init failed");
		}
	}

	private IEnumerator InitDecoderAsync(string path)
	{
		DebugLog("init Decoder Async");
		playerState = PlayerState.INTIALIZING;
		url = path;

		decoderId = -1;
		NativeInitDecoder(url, ref decoderId);

		// Check init decoder state;

		// Check video enabled;
		if (!videoDisabled)
		{

		}

		// Check audio enabled;
		if (!audioDisabled)
		{

		}

		playerState = PlayerState.INITIALIZED;

		return null;
	}

	private void CreateTextures()
	{
		ReleaseTextures();

		var nativeTexY = new IntPtr();
		var nativeTexU = new IntPtr();
		var nativeTexV = new IntPtr();

		var material = GetComponent<MeshRenderer>().sharedMaterial;

		if (NativeCreateTexture(ref nativeTexY, ref nativeTexU, ref nativeTexV))
		{
			if (nativeTexY != IntPtr.Zero)
			{
				videoTexY = Texture2D.CreateExternalTexture(videoWidth, videoHeight, TextureFormat.Alpha8, false, false, nativeTexY);
				material.SetTexture("_YTex", videoTexY);
			}
			if (nativeTexU != IntPtr.Zero)
			{
				videoTexU = Texture2D.CreateExternalTexture(videoWidth / 2, videoHeight / 2, TextureFormat.Alpha8, false, false, nativeTexU);
				material.SetTexture("_UTex", videoTexU);
			}
			if (nativeTexV != IntPtr.Zero)
			{
				videoTexV = Texture2D.CreateExternalTexture(videoWidth / 2, videoHeight / 2, TextureFormat.Alpha8, false, false, nativeTexV);
				material.SetTexture("_VTex", videoTexV);
			}
		}
		else
		{
			DebugLog("Failed to create native textures");
		}
	}

	private void ReleaseTextures()
	{
		SetTextures(null, null, null);

		videoTexY = null;
		videoTexU = null;
		videoTexV = null;
	}

	private void SetTextures(Texture ytex, Texture utex, Texture vtex)
	{
		var texMat = GetComponent<MeshRenderer>().material;
		texMat.SetTexture("_YTex", ytex);
		texMat.SetTexture("_UTex", utex);
		texMat.SetTexture("_VTex", vtex);
	}

	public void StartDecoding()
	{
		if (!NativeStart())
		{
			DebugLog("Failed to start video");
		}
		else
		{
			DebugLog("Started video playback");
		}

		StartCoroutine(CallPluginAtEndOfFrames());
	}

	public void DisableVideo(bool status)
	{
		SetVideoDisabledNative(status);
	}

	public void DisableAudio(bool status)
	{
		SetAudioDisabledNative(status);
	}

	public void Mute()
	{

	}

	private IEnumerator CallPluginAtEndOfFrames()
	{
		while (true)
		{
			yield return Yield.endOfFrame;

			SetTimeFromUnity(Time.timeSinceLevelLoad);

			GL.IssuePluginEvent(renderEventFunc, 1);
		}
	}

	private static void DebugLog(string message)
	{
		Debug.Log($"[VivistaPlayer] {message}");
	}
}
