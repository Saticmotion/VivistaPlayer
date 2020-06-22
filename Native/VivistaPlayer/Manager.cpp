#include "Manager.h"
#include "Decoder.h"

Manager::Manager()
{
	playerState = UNINITIALIZED;
	seekTime = 0.0;
	decoder = new Decoder();
}

Manager::~Manager()
{
	delete decoder;
	Stop();
}

void Manager::Init(const char* filePath)
{
	if (decoder == NULL || !decoder->Init(filePath))
	{
		playerState = INIT_FAIL;
	}
	else
	{
		playerState = INITIALIZED;
	}
}

void Manager::Start()
{
	if (decoder == NULL || 
		(playerState != INITIALIZED 
			&& playerState != PAUSE
			&& playerState != STOP))
	{
		return;
	}
	
	decodeThread = std::thread([&]() {
			if (!(decoder->GetVideoInfo().isEnabled || decoder->GetAudioInfo().isEnabled))
			{
				return;
			}

			playerState = PLAYING;

			while (playerState != STOP)
			{
				switch (playerState)
				{
					case PLAYING:
						if (!decoder->Decode()) {
							playerState = PLAY_EOF;
						}


						break;
					case SEEK:
						decoder->Seek(seekTime);
						playerState = PLAYING;
						break;
					case PLAY_EOF:
						break;
				}
			}
		});
}

void Manager::Seek(float seconds)
{
	if (playerState < INITIALIZED || playerState == SEEK)
	{
		return;
	}

	seekTime = seconds;
	playerState = SEEK;
}

void Manager::Stop()
{
	playerState = STOP;
	if (decodeThread.joinable())
	{
		decodeThread.join();
	}

	decoder = NULL;
	playerState = UNINITIALIZED;
}

Manager::PlayerState Manager::GetPlayerState()
{
	return playerState;
}

double Manager::GetVideoFrame(uint8_t** outputY, uint8_t** outputU, uint8_t** outputV)
{
	if (decoder == NULL || !decoder->GetVideoInfo().isEnabled || playerState == SEEK)
	{
		*outputY = *outputU = *outputV = NULL;
		return -1;
	}

	return decoder->GetVideoFrame(outputY, outputU, outputV);
}

double Manager::GetAudioFrame(uint8_t** outputFrame, int& frameSize)
{
	if (decoder == NULL || !decoder->GetAudioInfo().isEnabled || playerState == SEEK)
	{
		*outputFrame = NULL;
		return -1;
	}

	return decoder->GetAudioFrame(outputFrame, frameSize);
}

void Manager::FreeVideoFrame()
{
	if (decoder == NULL || !decoder->GetVideoInfo().isEnabled || playerState == SEEK) {
		return;
	}

	decoder->FreeVideoFrame();
}



void Manager::FreeAudioFrame()
{
	if (decoder == NULL || !decoder->GetAudioInfo().isEnabled || playerState == SEEK) {
		return;
	}

	decoder->FreeAudioFrame();
}

//void Manager::EnableVideo(bool isEnabled)
//{
//	if (decoder == NULL)
//	{
//		return;
//	}
//	decoder->videoEnable(isEnabled);
//}
//
//void Manager::EnableAudio(bool isEnabled)
//{
//	if (decoder == NULL)
//	{
//		return;
//	}
//
//	decoder->audioEnable(isEnabled);
//}

Decoder::VideoInfo Manager::getVideoInfo()
{
	return decoder->GetVideoInfo();
}

Decoder::AudioInfo Manager::getAudioInfo()
{
	return decoder->GetAudioInfo();
}

bool Manager::isVideoBufferEmpty()
{
	Decoder::VideoInfo* videoInfo = &(decoder->GetVideoInfo());
	return videoInfo->isEnabled && videoInfo->bufferState == Decoder::BufferState::EMPTY;
}

bool Manager::isVideoBufferFull()
{
	Decoder::VideoInfo* videoInfo = &(decoder->GetVideoInfo());
	return videoInfo->isEnabled && videoInfo->bufferState == Decoder::BufferState::FULL;
}
