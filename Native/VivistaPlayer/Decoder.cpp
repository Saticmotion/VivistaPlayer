#include <fstream>
#include <string>

#include "Decoder.h"
#include "Logger.h"

Decoder::Decoder()
{
	inputContext = NULL;
	videoStreamIndex = 0;
	videoStream = NULL;
	audioStreamIndex = 0;
	audioStream = NULL;
	videoCodec = NULL;
	audioCodec = NULL;
	videoCodecContext = NULL;
	audioCodecContext = NULL;
	av_init_packet(&packet);

	swrContext = NULL;

	videoBuffMax = 64;
	audioBuffMax = 128;

	videoInfo = {};
	audioInfo = {};
	isInitialized = false;
	isAudioAllChEnabled = false;
	useTCP = false;
}

Decoder::~Decoder()
{
	if (videoCodecContext != NULL)
	{
		avcodec_close(videoCodecContext);
		videoCodecContext = NULL;
	}

	if (audioCodecContext != NULL)
	{
		avcodec_close(audioCodecContext);
		audioCodecContext = NULL;
	}

	if (inputContext != NULL)
	{
		avformat_close_input(&inputContext);
		avformat_free_context(inputContext);
		inputContext = NULL;
	}

	if (swrContext != NULL)
	{
		swr_close(swrContext);
		swr_free(&swrContext);
		swrContext = NULL;
	}

	FlushBuffer(&videoFrames, &videoMutex);
	FlushBuffer(&audioFrames, &audioMutex);

	videoCodec = NULL;
	audioCodec = NULL;

	videoStream = NULL;
	audioStream = NULL;
	av_packet_unref(&packet);
}

bool Decoder::Init(const char* filePath)
{
	if (isInitialized)
	{
		return true;
	}

	if (filePath == NULL)
	{
		return false;
	}

	int errorCode;
	int st_index[AVMEDIA_TYPE_NB];
	double ctxDuration;

	if (inputContext == NULL)
	{
		inputContext = avformat_alloc_context();
	}

	AVDictionary* opts = NULL;
	if (useTCP)
	{
		av_dict_set(&opts, "rtsp_transport", "tcp", 0);
	}

	errorCode = avformat_open_input(&inputContext, filePath, NULL, &opts);
	av_dict_free(&opts);
	if (errorCode < 0)
	{
		LOG("avformat_open_input error(%x). \n", errorCode);
		return false;
	}

	errorCode = avformat_find_stream_info(inputContext, NULL);
	if (errorCode < 0)
	{
		LOG("avformat_find_stream_info error(%x). \n", errorCode);
		return false;
	}

	ctxDuration = (double)(inputContext->duration) / AV_TIME_BASE;

	videoStreamIndex = av_find_best_stream(inputContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (videoStreamIndex < 0)
	{
		LOG("video stream not found. \n");
		videoInfo.isEnabled = false;
	}
	else
	{
		videoInfo.isEnabled = true;
		videoStream = inputContext->streams[videoStreamIndex];
		videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);

		if (videoCodec == NULL)
		{
			LOG("Video codec not available. \n");
			return false;
		}

		videoCodecContext = NULL;
		videoCodecContext = avcodec_alloc_context3(videoCodec);
		errorCode = avcodec_parameters_to_context(videoCodecContext, inputContext->streams[videoStreamIndex]->codecpar);
		if (errorCode < 0) { return false; }

		AVDictionary* autoThread = nullptr;
		av_dict_set(&autoThread, "threads", "auto", 0);
		errorCode = avcodec_open2(videoCodecContext, videoCodec, &autoThread);
		av_dict_free(&autoThread);
		if (errorCode < 0) { return false; }

		//	Save the output video format
		videoInfo.width = videoCodecContext->width;
		videoInfo.height = videoCodecContext->height;
		videoInfo.totalTime = videoStream->duration <= 0 ? ctxDuration : videoStream->duration * av_q2d(videoStream->time_base);

		videoFrames.swap(decltype(videoFrames)());
	}



	audioStreamIndex = av_find_best_stream(inputContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audioStreamIndex < 0)
	{
		audioInfo.isEnabled = false;
	}
	else
	{
		audioInfo.isEnabled = true;
		audioStream = inputContext->streams[audioStreamIndex];
		audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);

		if (audioCodec == NULL)
		{
			return false;
		}

		audioCodecContext = NULL;
		audioCodecContext = avcodec_alloc_context3(audioCodec);
		errorCode = avcodec_parameters_to_context(audioCodecContext, inputContext->streams[audioStreamIndex]->codecpar);
		if (errorCode < 0) { return false; }

		errorCode = avcodec_open2(audioCodecContext, audioCodec, NULL);
		if (errorCode < 0) { return false; }

		int64_t inChannelLayout = av_get_default_channel_layout(audioCodecContext->channels);
		uint64_t outChannelLayout = isAudioAllChEnabled ? inChannelLayout : AV_CH_LAYOUT_STEREO;
		AVSampleFormat inSampleFormat = audioCodecContext->sample_fmt;
		AVSampleFormat outSampleFormat = AV_SAMPLE_FMT_FLT;
		int inSampleRate = audioCodecContext->sample_rate;
		int outSampleRate = inSampleRate;

		if (swrContext != NULL)
		{
			swr_close(swrContext);
			swr_free(&swrContext);
			swrContext = NULL;
		}

		swrContext = swr_alloc_set_opts(NULL,
										 outChannelLayout, outSampleFormat, outSampleRate,
										 inChannelLayout, inSampleFormat, inSampleRate,
										 0, NULL);


		int errorCode = swr_is_initialized(swrContext) == 0;
		if (errorCode < 0) { return false; }

		errorCode = swr_init(swrContext);
		if (errorCode < 0) { return false; }

		//	Save the output audio format
		audioInfo.channels = av_get_channel_layout_nb_channels(outChannelLayout);
		audioInfo.sampleRate = outSampleRate;
		audioInfo.totalTime = audioStream->duration <= 0 ? (double)(inputContext->duration) / AV_TIME_BASE : audioStream->duration * av_q2d(audioStream->time_base);;

		audioFrames.swap(decltype(audioFrames)());
	}

	isInitialized = true;

	return true;
}

bool Decoder::Decode()
{
	if (!isInitialized)
	{
		return false;
	}

	if (!IsBuffBlocked())
	{
		if (int errorCode = av_read_frame(inputContext, &packet) < 0)
		{
			return false;
		}

		if (videoInfo.isEnabled && packet.stream_index == videoStream->index)
		{
			UpdateVideoFrame();
		}
		else if (audioInfo.isEnabled && packet.stream_index == audioStream->index)
		{
			//UpdateAudioFrame();
		}

		av_packet_unref(&packet);
	}

	return true;
}

void Decoder::Seek(double time)
{
	if (!isInitialized)
	{
		return;
	}

	uint64_t timeStamp = (uint64_t)time * AV_TIME_BASE;

	if (av_seek_frame(inputContext, -1, timeStamp, AVSEEK_FLAG_BACKWARD) < 0)
	{
		return;
	}

	if (videoInfo.isEnabled)
	{
		if (videoCodecContext != NULL)
		{
			avcodec_flush_buffers(videoCodecContext);
		}
		FlushBuffer(&videoFrames, &videoMutex);
		videoInfo.lastTime = -1;
	}

	if (audioInfo.isEnabled)
	{
		if (audioCodecContext != NULL)
		{
			avcodec_flush_buffers(audioCodecContext);
		}
		FlushBuffer(&audioFrames, &audioMutex);
		audioInfo.lastTime = -1;
	}
}

void Decoder::StreamComponentOpen()
{

}

Decoder::VideoInfo Decoder::GetVideoInfo()
{
	return videoInfo;
}

Decoder::AudioInfo Decoder::GetAudioInfo()
{
	return audioInfo;
}

double Decoder::GetVideoFrame(unsigned char** outputY, unsigned char** outputU, unsigned char** outputV)
{
	std::lock_guard<std::mutex> lock(videoMutex);

	if (!isInitialized || videoFrames.size() == 0)
	{
		*outputY = *outputU = *outputV = NULL;
		return -1;
	}

	AVFrame* frame = videoFrames.front();
	*outputY = frame->data[0];
	*outputU = frame->data[1];
	*outputV = frame->data[2];
	// PTS
	int64_t timeStamp = frame->best_effort_timestamp;
	double timeInSec = av_q2d(videoStream->time_base) * timeStamp;
	videoInfo.lastTime = timeInSec;

	return timeInSec;
}

double Decoder::GetAudioFrame(unsigned char** outputFrame, int& frameSize)
{
	std::lock_guard<std::mutex> lock(audioMutex);
	if (!isInitialized || audioFrames.size() == 0)
	{
		*outputFrame = NULL;
		return -1;
	}

	AVFrame* frame = audioFrames.front();
	*outputFrame = frame->data[0];
	frameSize = frame->nb_samples;
	int64_t timeStamp = 0;
	//int64_t timeStamp = av_frame_get_best_effort_timestamp(frame);
	double timeInSec = av_q2d(audioStream->time_base) * timeStamp;
	audioInfo.lastTime = timeInSec;

	return timeInSec;
}

void Decoder::EnableVideo(bool isEnabled)
{
	if (videoStream == NULL)
	{
		return;
	}

	videoInfo.isEnabled = isEnabled;
}

void Decoder::EnableAudio(bool isEnabled)
{
	if (audioStream == NULL)
	{
		return;
	}

	audioInfo.isEnabled = isEnabled;
}

void Decoder::FreeVideoFrame()
{
	FreeFrontFrame(&videoFrames, &videoMutex);
}

void Decoder::FreeAudioFrame()
{
	FreeFrontFrame(&audioFrames, &audioMutex);
}

void Decoder::UpdateBufferState()
{
	if (videoInfo.isEnabled)
	{
		if (videoFrames.size() >= videoBuffMax)
		{
			videoInfo.bufferState = BufferState::FULL;
		}
		else if (videoFrames.size() == 0)
		{
			videoInfo.bufferState = BufferState::EMPTY;
		}
		else
		{
			videoInfo.bufferState = BufferState::NORMAL;
		}
	}

	if (audioInfo.isEnabled)
	{
		if (audioFrames.size() >= audioBuffMax)
		{
			audioInfo.bufferState = BufferState::FULL;
		}
		else if (audioFrames.size() == 0)
		{
			audioInfo.bufferState = BufferState::EMPTY;
		}
		else
		{
			audioInfo.bufferState = BufferState::NORMAL;
		}
	}
}

bool Decoder::IsBuffBlocked()
{
	if (videoInfo.isEnabled && videoInfo.bufferState == BufferState::FULL)
	{
		return true;
	}

	if (audioInfo.isEnabled && audioInfo.bufferState == BufferState::FULL)
	{
		return true;
	}

	return false;
}

/**
 * This function is called for updating a video frame.
 *
 * This thread reads in packets from the video queue, packet_queue_get(), decodes
 * the video packets into a frame, and then calls the queue_picture() function to
 * put the processed frame into the picture queue.
 *
 */
void Decoder::UpdateVideoFrame()
{
	AVFrame* frame = av_frame_alloc();
	clock_t start = clock();
	int errorCode = 0;
	double pts;
	
	errorCode = avcodec_send_packet(videoCodecContext, &packet);
	errorCode = avcodec_receive_frame(videoCodecContext, frame);

	// TODO PTS

	//if (avcodec_decode_video2(videoCodecContext, frame, &isFrameAvailable, &packet) < 0)
	//{
	//	return;
	//}
	printf("UpdateVideoFrame = %f\n", (float)(clock() - start) / CLOCKS_PER_SEC);
	if (errorCode >= 0)
	{
		std::lock_guard<std::mutex> lock(videoMutex);
		videoFrames.push(frame);
		UpdateBufferState();
	}
}

void Decoder::UpdateAudioFrame()
{
	int isFrameAvailable = 0;
	AVFrame* frameDecoded = av_frame_alloc();
	int errorCode = 0;

	// TODO
	errorCode = avcodec_receive_frame(audioCodecContext, frameDecoded);

	//if (avcodec_decode_audio4(audioCodecContext, frameDecoded, &isFrameAvailable, &packet) < 0)
	//{
	//	return;
	//}

	AVFrame* frame = av_frame_alloc();
	frame->sample_rate = frameDecoded->sample_rate;
	frame->channel_layout = av_get_default_channel_layout(audioInfo.channels);
	frame->format = AV_SAMPLE_FMT_FLT;	//	For Unity format.
	frame->best_effort_timestamp = frameDecoded->best_effort_timestamp;
	swr_convert_frame(swrContext, frame, frameDecoded);

	std::lock_guard<std::mutex> lock(audioMutex);
	audioFrames.push(frame);
	UpdateBufferState();
	av_frame_free(&frameDecoded);
}

void Decoder::FreeFrontFrame(std::queue<AVFrame*>* frameBuff, std::mutex* mutex)
{
	std::lock_guard<std::mutex> lock(*mutex);
	if (!isInitialized || frameBuff->size() == 0)
	{
		return;
	}

	AVFrame* frame = frameBuff->front();
	av_frame_free(&frame);
	frameBuff->pop();
	UpdateBufferState();
}

//	frameBuff.clear would only clean the pointer rather than whole resources. So we need to clear frameBuff by ourself.
void Decoder::FlushBuffer(std::queue<AVFrame*>* frameBuff, std::mutex* mutex)
{
	std::lock_guard<std::mutex> lock(*mutex);
	while (!frameBuff->empty())
	{
		av_frame_free(&(frameBuff->front()));
		frameBuff->pop();
	}
}
