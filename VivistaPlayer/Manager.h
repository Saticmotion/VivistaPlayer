#pragma once
#include "Decoder.h"
#include <thread>
#include <mutex>
#include <memory>

class Manager
{
public:
	Manager();
	~Manager();

	enum PlayerState
	{
		INIT_FAIL = -1,
		UNINITIALIZED, 
		INITIALIZED, 
		PLAYING, 
		SEEK, 
		BUFFERING, 
		PLAY_EOF, 
		STOP,
		PAUSE
	};

	void Init(const char* filePath);
	void Start();
	void Stop();
	void Seek(float sec);
	void Pause();

	PlayerState GetPlayerState();
	double GetVideoFrame(uint8_t** outputY, uint8_t** outputU, uint8_t** outputV);
	double GetAudioFrame(uint8_t** outputFrame, int& frameSize);
	void FreeVideoFrame();
	void FreeAudioFrame();
	void EnableVideo(bool enable);
	void EnableAudio(bool enable);


private:
	PlayerState mPlayerState;
	VideoState* mVideoState;
	double mSeekTime;
	bool mPause;

	std::thread mDecodeThread;
};