using UnityEngine;
using UnityEngine.Events;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class VivistaPlayer : MonoBehaviour
{
    // Native plugin rendering events are only called if a plugin is used
    // by some script. This means we have to DllImport at least
    // one function in some active script.
    // For this example, we'll call into plugin's SetTimeFromUnity
    // function and pass the current time so the plugin can animate.

    //#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
    //	[DllImport ("__Internal")]
    //#else
    //	[DllImport("VivistaPlayer")]
    //#endif
    //	private static extern void SetTimeFromUnity(float t);

    // We'll also pass native pointer to a texture in Unity.
    // The plugin will fill texture data from native code.
    //#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
    //	[DllImport ("__Internal")]
    //#else
    //	[DllImport("VivistaPlayer")]
    //#endif
    //    private static extern void SetTextureFromUnity(IntPtr texture, int w, int h);

    //#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
    //	[DllImport ("__Internal")]
    //#else
    //	[DllImport("VivistaPlayer")]
    //#endif
    //	private static extern void DestroyDecoderNative();

    //#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
    //	[DllImport ("__Internal")]
    //#else
    //	[DllImport("VivistaPlayer")]
    //#endif
    //	private static extern void setVideoDisabledNative(bool status);

    //#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
    //	[DllImport ("__Internal")]
    //#else
    //	[DllImport("VivistaPlayer")]
    //#endif
    //	private static extern void setAudioDisabledNative(bool status);

#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
	[DllImport ("__Internal")]
#else
    [DllImport("VivistaPlayer")]
#endif
	private static extern IntPtr GetRenderEventFunc();

#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
	[DllImport ("__Internal")]
#else
	[DllImport("VivistaPlayer")]
#endif
	private static extern void nativeInitDecoder(string path, ref int id);

#if (UNITY_IOS || UNITY_TVOS || UNITY_WEBGL) && !UNITY_EDITOR
		[DllImport ("__Internal")]
#else
    [DllImport("VivistaPlayer")]
#endif
    private static extern int nativeGetPlayerState(int id);

#if !UNITY_EDITOR
	[DllImport ("__Internal")]
	private static extern void RegisterPlugin();
#endif

    #region Public Variables
    /**
	 * Whether the content will start playing as soon as the component
	 * awakes.
	 */
    public bool playOnAwake = false;
	/**
	 * 	The file or HTTP URL that the videoplayer reads content from.
	 */
	public string url = null;
	/**
	 * 	Playback speed of the video.
	 */
	public float playback = 1.0f;

	public enum PlayerState 
	{
		NOT_INITIALIZED,
		INTIALIZING,
		INITIALIZED,
		PLAY,
		STOP,
		PAUSE,
		SEEK,
		BUFFERING,
		EOF
	}

	#region Events
	/**
	 * Invoked when the videoplayer is succesfully initialized.
	 */
	public UnityEvent prepareCompleted = null;
	public UnityEvent started = null;

	#endregion

	#endregion
	#region Private Variables
	private int decoderID = -1;

	/**
	 * Invoked when the video has finished playing.
	*/
	private UnityEvent loopPointReached = null;

	private const string LOG_TAG = "[VivistaPlayer]";

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
	private List<float> audioBuffer = null;

	private float volume;

	// Player
	private PlayerState lastState = VivistaPlayer.PlayerState.NOT_INITIALIZED;
	private PlayerState playerState = VivistaPlayer.PlayerState.NOT_INITIALIZED;

	private Texture2D videoTexY = null;
	private Texture2D videoTexU = null;
	private Texture2D videoTexV = null;
    #endregion

    #region Lifecycle Unity
    // Start is called before the first frame update
    void Start()
	{
#if !UNITY_EDITOR
		RegisterPlugin();
#endif
		//CreateTextureAndPassToPlugin();
		//yield return StartCoroutine("CallPluginAtEndOfFrames");
		GL.IssuePluginEvent(GetRenderEventFunc(), 1);

		RegisterDebugCallback(new DebugCallback(DebugMethod));
	}

	void Awake() 
	{
		if (playOnAwake) 
		{
			print(LOG_TAG + " play on awake");
			if (prepareCompleted == null)
			{
				prepareCompleted = new UnityEvent();
			}
			prepareCompleted.AddListener(startDecoding);
			prepare(url);
		}
	}

	void Update()
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
    #endregion

    #region Private Methods
    private void prepare(string path)
	{
		StartCoroutine(initDecoder(path));
	}

	IEnumerator initDecoder(string path)
	{
		print(LOG_TAG + " init Decoder.");
		playerState = PlayerState.INTIALIZING;

		url = path;
		decoderID = -1;
		nativeInitDecoder(path, ref decoderID);

		int result = 0;
		do
		{
			yield return null;
			result = nativeGetPlayerState(decoderID);
		} while (!(result == 1 || result == -1));

		if (result == 1) 
		{
			print(LOG_TAG + "Init success.");
		}
	}

    //IEnumerator InitDecoderAsync(string path)
    //{ 
    //	print(LOG_TAG + " init Decoder.");
    //	playerState = PlayerState.INTIALIZING;
    //	url = path;

    //	InitDecoderNative(url);

    //	// Check init decoder state;

    //	// Check video enabled;
    //	if (!videoDisabled)
    //	{ 

    //	}

    //	// Check audio enabled;
    //	if (!audioDisabled)
    //	{

    //	}

    //	playerState = PlayerState.INITIALIZED;

    //	return null;
    //}

    //private void CreateTextures()
    //{
    //	ReleaseTextures();

    //	IntPtr nativeTexturePtrY = new IntPtr();
    //	IntPtr nativeTexturePtrU = new IntPtr();
    //	IntPtr nativeTexturePtrV = new IntPtr();

    //	nativeCreateTexture(ref nativeTexturePtrY, ref nativeTexturePtrU, ref nativeTexturePtrV);

    //	videoTexY = Texture2D.CreateExternalTexture(
    //		videoWidth, videoHeight, TextureFormat.Alpha8, false, false, nativeTexturePtrY);
    //	videoTexU = Texture2D.CreateExternalTexture(
    //		videoWidth / 2, videoHeight / 2, TextureFormat.Alpha8, false, false, nativeTexturePtrU);
    //	videoTexV = Texture2D.CreateExternalTexture(
    //		videoWidth / 2, videoHeight / 2, TextureFormat.Alpha8, false, false, nativeTexturePtrV);
    //}

    //private void ReleaseTextures()
    //{
    //	setTextures(null, null, null);

    //	videoTexY = null;
    //	videoTexU = null;
    //	videoTexV = null;

    //	useDefault = true;
    //}

    //private void setTextures(Texture ytex, Texture utex, Texture vtex)
    //{
    //	Material texMat = GetComponent<MeshRenderer>().material;
    //	texMat.SetTexture("_YTex", ytex);
    //	texMat.SetTexture("_UTex", utex);
    //	texMat.SetTexture("_VTex", vtex);
    //}

    #endregion

    #region Public Methods

    public void startDecoding()
	{ 
		
	}

	//public void DisableVideo(bool status) 
	//{
	//	setVideoDisabledNative(status);
	//}

	//public void DisableAudio(bool status)
	//{
	//	setAudioDisabledNative(status);
	//}

	//public void mute()
	//{ 

	//}

	//private IEnumerator CallPluginAtEndOfFrames()
	//{
	//	while (true)
	//	{
	//		// Wait until all frame rendering is done
	//		yield return new WaitForEndOfFrame();

	//		// Set time for the plugin
	//		SetTimeFromUnity(Time.timeSinceLevelLoad);

	//		// Issue a plugin event with arbitrary integer identifier.
	//		// The plugin can distinguish between different
	//		// things it needs to do based on this ID.
	//		// For our simple plugin, it does not matter which ID we pass here.
	//		GL.IssuePluginEvent(GetRenderEventFunc(), 1);
	//	}
	//}

	#endregion

	private delegate void DebugCallback(string message);
	[DllImport("VivistaPlayer")]
	private static extern void RegisterDebugCallback(DebugCallback callback);

	private static void DebugMethod(string message)
	{
		Debug.Log("VivistaPlayer: " + message);
	}
}
