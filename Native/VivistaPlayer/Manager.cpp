#include "Manager.h"
#include "Decoder.h"

Manager::Manager()
{
	mPlayerState = UNINITIALIZED;
	mSeekTime = 0.0;
	mDecoder = std::make_unique<Decoder>();
}

Manager::~Manager()
{
	Stop();
}

void Manager::Init(const char* filePath)
{
	if (mDecoder == NULL || !mDecoder->init(filePath))
	{
		mPlayerState = INIT_FAIL;
	}
	else
	{
		mPlayerState = INITIALIZED;
	}
}

void Manager::Start()
{
	if (mDecoder == NULL || mPlayerState != UNINITIALIZED)
	{
		return;
	}
	
	mDecodeThread = std::thread([&]() {
			if (!(mDecoder->getVideoInfo().isEnabled || mDecoder->getAudioInfo().isEnabled))
			{
				return;
			}

			mPlayerState = PLAYING;

			while (mPlayerState != STOP)
			{
				switch (mPlayerState)
				{
				case PLAYING:
					if (!mDecoder->decode()) {
						mPlayerState = PLAY_EOF;
					}
					break;
				case SEEK:
					mDecoder->seek(mSeekTime);
					mPlayerState = PLAYING;
					break;
				case PLAY_EOF:
					break;
				}
			}
		});
}

void Manager::Seek(float seconds)
{
	if (mPlayerState < INITIALIZED || mPlayerState == SEEK)
	{
		return;
	}

	mSeekTime = seconds;
	mPlayerState = SEEK;
}

void Manager::Stop()
{
	mPlayerState = STOP;
	if (mDecodeThread.joinable())
	{
		mDecodeThread.join();
	}

	mDecoder = NULL;
	mPlayerState = UNINITIALIZED;
}

Manager::PlayerState Manager::GetPlayerState()
{
	return mPlayerState;
}

double Manager::GetVideoFrame(uint8_t** outputY, uint8_t** outputU, uint8_t** outputV)
{
	if (mDecoder == NULL || !mDecoder->getVideoInfo().isEnabled || mPlayerState == SEEK)
	{
		*outputY = *outputU = *outputV = NULL;
		return -1;
	}

	return mDecoder->getVideoFrame(outputY, outputU, outputV);
}

double Manager::GetAudioFrame(uint8_t** outputFrame, int& frameSize)
{
	if (mDecoder == NULL || !mDecoder->getAudioInfo().isEnabled || mPlayerState == SEEK)
	{
		*outputFrame = NULL;
		return -1;
	}

	return mDecoder->getAudioFrame(outputFrame, frameSize);
}

void Manager::FreeVideoFrame()
{
	if (mDecoder == NULL || !mDecoder->getVideoInfo().isEnabled || mPlayerState == SEEK) {
		return;
	}

	mDecoder->freeVideoFrame();
}



void Manager::FreeAudioFrame()
{
	if (mDecoder == NULL || !mDecoder->getAudioInfo().isEnabled || mPlayerState == SEEK) {
		return;
	}

	mDecoder->freeAudioFrame();
}

//void Manager::EnableVideo(bool isEnabled)
//{
//	if (mDecoder == NULL)
//	{
//		return;
//	}
//	mDecoder->videoEnable(isEnabled);
//}
//
//void Manager::EnableAudio(bool isEnabled)
//{
//	if (mDecoder == NULL)
//	{
//		return;
//	}
//
//	mDecoder->audioEnable(isEnabled);
//}

Decoder::VideoInfo Manager::getVideoInfo()
{
	return Decoder::VideoInfo();
}

Decoder::AudioInfo Manager::getAudioInfo()
{
	return Decoder::AudioInfo();
}

bool Manager::isVideoBufferEmpty()
{
	Decoder::VideoInfo* videoInfo = &(mDecoder->getVideoInfo());
	Decoder::BufferState EMPTY = Decoder::BufferState::EMPTY;
	return videoInfo->isEnabled && videoInfo->bufferState == EMPTY;
}

bool Manager::isVideoBufferFull()
{
	Decoder::VideoInfo* videoInfo = &(mDecoder->getVideoInfo());
	Decoder::BufferState FULL = Decoder::BufferState::FULL;
	return videoInfo->isEnabled && videoInfo->bufferState == FULL;
}
