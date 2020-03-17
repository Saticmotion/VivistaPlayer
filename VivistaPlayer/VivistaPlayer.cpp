//#include "Unity/IUnityRenderingExtensions.h"
//
//namespace
//{
//	void TextureUpdateCallback(int eventID, void* data)
//	{
//		auto event = static_cast<UnityRenderingExtEventType>(eventID);
//
//		if (event == kUnityRenderingExtEventUpdateTextureBeginV2)
//		{
//			auto params = reinterpret_cast<UnityRenderingExtTextureUpdateParamsV2*>(data);
//			// TODO (Jeroen) 
//		}
//		else if (event == kUnityRenderingExtEventUpdateTextureEndV2)
//		{
//			auto params = reinterpret_cast<UnityRenderingExtTextureUpdateParamsV2*>(data);
//			// TODO (Jeroen) 
//		}
//	}
//}
//
//extern "C" UnityRenderingEventAndData UNITY_INTERFACE_EXPORT Vivista_GetTextureUpdateCallback()
//{
//	return TextureUpdateCallback;
//}