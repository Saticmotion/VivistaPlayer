#include "Manager.h"

Manager::Manager()
{
	mPlayerState = UNINITIALIZED;
	mSeekTime = 0.0;
	mVideoState = (VideoState*)av_mallocz(sizeof(VideoState));
}

Manager::~Manager()
{
	// stop decoding
	av_free(mVideoState);
}

void Manager::Init(const char* filePath)
{
	if (mVideoState == NULL || !decoder_init(filePath))
	{
		mPlayerState = INIT_FAIL;
		av_free(mVideoState);
	}
	else
	{
		mPlayerState = INITIALIZED;
	}
}

void Manager::Start()
{
	if (mVideoState == NULL || mPlayerState != UNINITIALIZED)
	{
		return;
	}
	
	mDecodeThread = std::thread([&]() {
			if (!(mVideoState->video_enabled || mVideoState->audio_enabled))
			{
				return;
			}

			mPlayerState = PLAYING;

			while (mPlayerState != STOP)
			{
				switch (mPlayerState)
				{
				case PLAYING:
					if (!decode(mVideoState)) {
						mPlayerState = PLAY_EOF;
					}
					break;
				case SEEK:
					seek_stream(mVideoState, mSeekTime);
					mPlayerState = PLAYING;
					break;
				case PAUSE:
					pause_stream(mVideoState, mPause);
					break;
				case PLAY_EOF:
					break;
				}
			}
		});
}

void Manager::Seek(float sec)
{
	if (mPlayerState < INITIALIZED || mPlayerState == SEEK)
	{
		return;
	}

	mSeekTime = sec;
	mPlayerState = SEEK;
}

void Manager::Stop()
{
	mPlayerState = STOP;
	if (mDecodeThread.joinable)
	{
		mDecodeThread.join();
	}

	mVideoState = NULL;
	mPlayerState = UNINITIALIZED;
}

void Manager::Pause()
{
	if (mPlayerState < INITIALIZED || mPlayerState == PAUSE)
	{
		return;
	}

	mPlayerState = PAUSE;
}

Manager::PlayerState Manager::GetPlayerState()
{
	return mPlayerState;
}

double Manager::GetVideoFrame(uint8_t** outputY, uint8_t** outputU, uint8_t** outputV)
{
	if (mVideoState == NULL || !mVideoState->video_enabled || mPlayerState == SEEK)
	{
		*outputY = *outputU = *outputV = NULL;
		return -1;
	}

	return get_video_frame(outputY, outputU, outputV);
}

double Manager::GetAudioFrame(uint8_t** outputFrame, int& frameSize)
{
	if (mVideoState == NULL || !mVideoState->audio_enabled || mPlayerState == SEEK)
	{
		*outputFrame = NULL;
		return -1;
	}

	return get_audio_frame(outputFrame, frameSize);
}