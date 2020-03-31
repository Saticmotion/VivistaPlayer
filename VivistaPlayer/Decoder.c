#include "Decoder.h"

static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = { 0 };

AVPacket flush_pkt;

/**
 * Initialize the videostate and find the available video- and audiostreams
 *
 * @param   the filepath of the video
 *
 * @return  < 0 in case of error, 0 otherwise.
 */
bool decoder_init(VideoState* videoState, const char* filepath)
{
	if (!filepath)
	{
		printf("Path is NULL.\n");
		return false;
	}

	int ret;
	int st_index[AVMEDIA_TYPE_NB];
	videoState->videoStream = -1;
	videoState->audioStream = -1;
	double ctxDuration = 0;
	
	AVFormatContext* inputContext = NULL;
	inputContext = avformat_alloc_context();
	if (!inputContext)
	{
		printf("Could not allocate context.\n");
		return false;
	}

	ret = avformat_open_input(&inputContext, videoState->filename, NULL, NULL);
	if (ret < 0)
	{
		printf("Could not open file %s.\n", videoState->filename);
		return false;
	}
	videoState->pFormatCtx = inputContext;

	ret = avformat_find_stream_info(inputContext, NULL);
	if (ret < 0)
	{
		printf("Could not find stream information %s\n", videoState->filename);
		return false;
	}

	ctxDuration = (double)(inputContext->duration) / AV_TIME_BASE;

	// Select a requested stream
	for (i = 0; i < inputContext->nb_streams; i++) {
		AVStream* st = inputContext->streams[i];
		enum AVMediaType type = st->codecpar->codec_type;
		st->discard = AVDISCARD_ALL;
		if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
		{
			if (avformat_match_stream_specifier(inputContext, st, wanted_stream_spec[type]) > 0)
			{
				st_index[type] = i;
			}
		}
	}
	for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
		if (wanted_stream_spec[i] && st_index[i] == -1) {
			st_index[i] = INT_MAX;
		}
	}

	if (videoState->video_enabled)
	{
		st_index[AVMEDIA_TYPE_VIDEO] = 
			av_find_best_stream(inputContext, 
				AVMEDIA_TYPE_VIDEO, 
				st_index[AVMEDIA_TYPE_VIDEO], 
				-1, NULL, 0);
	}


	if (videoState->audio_enabled)
	{
		st_index[AVMEDIA_TYPE_AUDIO] =
			av_find_best_stream(inputContext,
				AVMEDIA_TYPE_AUDIO,
				st_index[AVMEDIA_TYPE_AUDIO],
				st_index[AVMEDIA_TYPE_VIDEO]
				-1, NULL, 0);
	}

	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
	{
		AVStream* st = inputContext->streams[st_index[AVMEDIA_TYPE_VIDEO]];
		AVCodecParameters* codecpar = st->codecpar;
		AVRational sar = av_guess_sample_aspect_ratio(inputContext, st, NULL);
		if (codecpar->width)
		{
			set_default_window_size(codecpar->width, codecpar->height, sar);
		}
		ret = stream_component_open(videoState, st_index[AVMEDIA_TYPE_VIDEO]);
	}
	else
	{
		videoState->video_enabled = false;
	}

	if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
		ret = stream_component_open(videoState, st_index[AVMEDIA_TYPE_AUDIO]);
	}
	else
	{
		videoState->audio_enabled = false;
	}

    av_init_packet(&flush_pkt);
    flush_pkt.data = (uint8_t*)"FLUSH";

	return true;
}

/**
 * Opens Audio and Video Streams. If all codecs are retrieved correctly, starts
 * an infinite loop to read AVPackets from the global VideoState AVFormatContext.
 * Based on their stream index, each packet is placed in the appropriate queue.
 *
 * @param   arg
 *
 * @return      < 0 in case of error, 0 otherwise.
 */
int decode_thread(void* arg)
{
	VideoState* videoState = (VideoState*)arg;

	AVFormatContext* pFormatCtx = NULL;
	int ret = avformat_open_input(&pFormatCtx, videoState->filename, NULL, NULL);
	if (ret < 0)
	{
		printf("Could not open file %s.\n", videoState->filename);
		return -1;
	}

	videoState->videoStream = -1;
	videoState->audioStream = -1;

	global_video_state = videoState;

	videoState->pFormatCtx = pFormatCtx;

	ret = avformat_find_stream_info(pFormatCtx, NULL);
	if (ret < 0)
	{
		printf("Could not find stream information %s\n", videoState->filename);
		return -1;
	}

	av_dump_format(pFormatCtx, 0, videoState->filename, 0);

	int videoStream = -1;
	int audioStream = -1;

	double ctxDuration = (double)(pFormatCtx->duration) / AV_TIME_BASE;

	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
			videoStream < 0)
		{
			videoStream = i;
		}

		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
			audioStream < 0)
		{
			audioStream = i;
		}
	}

	if (videoStream == -1)
	{
		printf("Could not find video stream.\n");
		goto fail;
	}
	else
	{
		ret = stream_component_open(videoState, videoStream);

		if (ret < 0)
		{
			printf("Could not open video codec.\n");
			goto fail;
		}
	}

	if (audioStream == -1)
	{
		printf("Could not find audio stream.\n");
		goto fail;
	}
	else
	{
		ret = stream_component_open(videoState, audioStream);

		if (ret < 0)
		{
			printf("Could not open audio codec.\n");
			goto fail;
		}
	}

	if (videoState->videoStream < 0 || videoState->audioStream < 0)
	{
		printf("Could not open codecs: %s.\n", videoState->filename);
		goto fail;
	}

	AVPacket* packet = av_packet_alloc();
	if (packet == NULL)
	{
		printf("Could not alloc packet.\n");
		goto fail;
	}

	for (;;)
	{
		if (videoState->quit)
		{
			break;
		}

		if (videoState->seek_req)
		{
			int video_stream_index = -1;
			int audio_stream_index = -1;
			int64_t seek_target = videoState->seek_pos;

			if (videoState->videoStream >= 0)
			{
				video_stream_index = videoState->videoStream;
			}

			if (videoState->audioStream >= 0)
			{
				audio_stream_index = videoState->audioStream;
			}

			if (video_stream_index >= 0 && audio_stream_index >= 0)
			{
				seek_target = av_rescale_q(seek_target, AV_TIME_BASE_Q, pFormatCtx->streams[video_stream_index]->time_base);

				seek_target = av_rescale_q(seek_target, AV_TIME_BASE_Q, pFormatCtx->streams[audio_stream_index]->time_base);
			}

			ret = av_seek_frame(videoState->pFormatCtx, video_stream_index, seek_target, videoState->seek_flags);

			ret &= av_seek_frame(videoState->pFormatCtx, audio_stream_index, seek_target, videoState->seek_flags);

			if (ret < 0)
			{
				fprintf(stderr, "%s: error while seeking\n", videoState->pFormatCtx->filename);
			}
			else
			{
				if (videoState->videoStream >= 0)
				{
					packet_queue_flush(&videoState->videoq);
					packet_queue_put(&videoState->videoq, &flush_pkt);
				}

				if (videoState->audioStream >= 0)
				{
					packet_queue_flush(&videoState->audioq);
					packet_queue_put(&videoState->audioq, &flush_pkt);
				}
			}

			videoState->seek_req = 0;
		}

		if (videoState->audioq.size > MAX_AUDIOQ_SIZE || videoState->videoq.size > MAX_VIDEOQ_SIZE)
		{
			SDL_Delay(10);
			continue;
		}

		ret = av_read_frame(videoState->pFormatCtx, packet);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				videoState->quit = 1;
				break;
			}
			else if (videoState->pFormatCtx->pb->error == 0)
			{
				SDL_Delay(10);
				continue;
			}
			else
			{
				break;
			}
		}

		if (packet->stream_index == videoState->videoStream)
		{
			packet_queue_put(&videoState->videoq, packet);
		}
		else if (packet->stream_index == videoState->audioStream)
		{
			packet_queue_put(&videoState->audioq, packet);
		}
		else
		{
			av_packet_unref(packet);
		}
	}

	while (!videoState->quit)
	{
		SDL_Delay(100);
	}

fail:
	{
		SDL_Event event;
		event.type = FF_QUIT_EVENT;
		event.user.data1 = videoState;

		SDL_PushEvent(&event);

		return -1;
	};
}

/**
 * Retrieves the AVCodec and initializes the AVCodecContext for the given AVStream
 * index.
 *
 * @param   videoState      the global VideoState reference used to save info
 *                          related to the media being played.
 * @param   stream_index    the stream index obtained from the AVFormatContext.
 *
 * @return                  < 0 in case of error, 0 otherwise.
 */
int stream_component_open(VideoState* videoState, int stream_index)
{
	AVFormatContext* inputContext = videoState->pFormatCtx;

	if (stream_index < 0 || stream_index >= inputContext->nb_streams)
	{
		printf("Invalid stream index.");
		return -1;
	}

	AVCodecContext* codecContext = NULL;
	codecContext = avcodec_alloc_context3(codecContext);
	if (!codecContext)
	{
		printf("Could allocate a codec context.\n");
		return -1;
	}

	int ret = avcodec_parameters_to_context(codecContext, inputContext->streams[stream_index]->codecpar);
	if (ret < 0)
	{
		printf("Could not copy codec context.\n");
		return -1;
	}

	AVCodec* codec = NULL;
	codec = avcodec_find_decoder(codecContext->codec_id);
	if (codec == NULL)
	{
		printf("Unsupported codec.\n");
		return -1;
	}

	switch (codecContext->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:
	{
		videoState->audioStream = stream_index;
		videoState->audio_st = inputContext->streams[stream_index];
		videoState->audio_ctx = codecContext;
		videoState->audio_buf_size = 0;
		videoState->audio_buf_index = 0;

		if (avcodec_open2(codecContext, codec, NULL) < 0)
		{
			printf("Unsupported codec.\n");
			return -1;
		}

		// TODO (Jeroen) Initializer SwrContext
		init_swr_context();

		memset(&videoState->audio_pkt, 0, sizeof(videoState->audio_pkt));

		packet_queue_init(&videoState->audioq);
	}
	break;

	case AVMEDIA_TYPE_VIDEO:
	{
		videoState->videoStream = stream_index;
		videoState->video_st = inputContext->streams[stream_index];
		videoState->video_ctx = codecContext;

		// Don't forget to initialize the frame timer and the initial
		// previous frame delay: 1ms = 1e-6s
		videoState->frame_timer = (double)av_gettime() / 1000000.0;
		videoState->frame_last_delay = 40e-3;
		videoState->video_current_pts_time = av_gettime();

		AVDictionary* autoThread = NULL;
		av_dict_set(&autoThread, "threads", "auto", 0);
		ret = avcodec_open2(codecContext, codec, &autoThread);
		av_dict_free(&autoThread);
		if (ret < 0)
		{
			printf("Unsupported codec.\n");
			return -1;
		}

		videoState->sws_ctx = sws_getContext(
			videoState->video_ctx->width,
			videoState->video_ctx->height,
			videoState->video_ctx->pix_fmt,
			videoState->video_ctx->width,
			videoState->video_ctx->height,
			AV_PIX_FMT_YUV420P,
			SWS_BILINEAR,
			NULL,
			NULL,
			NULL
			);

		packet_queue_init(&videoState->videoq);
	}
	break;
	default:
	{

	}
	break;
	}

	return 0;
}

void enable_video(VideoState* videoState, bool enable)
{
	if (!videoState->video_st)
	{
		printf("Video stream not found. \n");
		return;
	}

	videoState->video_enabled = enable;
}

void select_video_stream(int stream_index)
{
	wanted_stream_spec[AVMEDIA_TYPE_VIDEO] = stream_index;
}

void enable_audio(VideoState* videoState, bool enable)
{
	if (!videoState->audio_st)
	{
		printf("Video stream not found. \n");
		return;
	}

	videoState->audio_enabled = enable;
}

void select_audio_stream(int stream_index)
{
	wanted_stream_spec[AVMEDIA_TYPE_VIDEO] = stream_index;
}

bool decode(VideoState* videoState)
{
	// TODO (Jeroen) Check initialized
	AVPacket* packet = av_packet_alloc();
	int ret;

	ret = av_read_frame(videoState->pFormatCtx, &packet);

	if (ret < 0)
	{
		return false;
	}

	if (videoState->video_enabled && packet->stream_index == videoState->video_st->index)
	{
		update_video_frame(videoState);
	}
	else if (videoState->audio_enabled && packet->stream_index == videoState->audio_st->index)
	{
		update_audio_frame(videoState);
	}
	return true;
}

double get_video_frame(unsigned char** outputY, unsigned char** outputU, unsigned char** outputV)
{
	int64_t timeStamp = 0;
	double timeInSec = 0;

	*outputY = frame->data[0];
	*outputU = frame->data[1];
	*outputV = frame->data[2];

	return timeInSec;
}

void update_video_frame(VideoState* videoState)
{
	int isFrameAvailable = 0;
	AVFrame* frame = av_frame_alloc();
	if (!frame)
	{
		return;
	}

	// TODO take packet of queue

	if (avcodec_decode_video2(videoState->video_ctx, frame, &isFrameAvailable, &mPacket) < 0) {
		return;
	}


	if (isFrameAvailable) {
		
		updateBufferState();
	}
}


void free_video_frame()
{
	
}

double get_audio_frame(unsigned char** outputFrame, int frameSize)
{
	int64_t timeStamp = 0;
	double timeInSec = 0;

	return timeInSec;
}


void update_audio_frame(VideoState* videoState) {
	int isFrameAvailable = 0;
	AVFrame* frameDecoded = av_frame_alloc();
	if (!frameDecoded)
	{
		return;
	}
	if (avcodec_decode_audio4(videoState, frameDecoded, &isFrameAvailable, &mPacket) < 0) {
		return;
	}

	AVFrame* frame = av_frame_alloc();
	frame->sample_rate = frameDecoded->sample_rate;
	frame->channel_layout = av_get_default_channel_layout(mAudioInfo.channels);
	frame->format = AV_SAMPLE_FMT_FLT;	//	For Unity format.
	frame->best_effort_timestamp = frameDecoded->best_effort_timestamp;
	swr_convert_frame(videoState->swr_ctx, frame, frameDecoded);

	updateBufferState();
	av_frame_free(&frameDecoded);
}

void free_audio_frame()
{

}

void seek(VideoState* videoState, double time)
{
	uint64_t timeStamp = (uint64_t)time * AV_TIME_BASE;

}

double get_video_frame(VideoState* videoState, unsigned char** outputY, unsigned char** outputU, unsigned char** outputV)
{

	VideoPicture* videoPicture;
	videoPicture = &videoState->pictq[videoState->pictq_rindex];

	// lock mutex video
	if (videoPicture->frame)
	{
		*outputY = videoPicture->frame->data[0];
		*outputU = videoPicture->frame->data[1];
		*outputV = videoPicture->frame->data[2];

		int64_t timeStamp = av_frame_get_best_effort_timestamp(videoPicture->frame);
		double timeInSec = av_q2d(videoState->videoStream->time_base) * timeStamp;
	}




	return timeInSec;
}

/**
  * Initialize the given PacketQueue.
  *
  * @param q	the PacketQueue to be initialized.
  */
void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	if (!q->mutex)
	{
		printf("SDL_CreateMutex Error: %s.\n", SDL_GetError());
		return;
	}
	q->cond = SDL_CreateCond();
	if (!q->cond)
	{
		printf("SDL_CreateCond Error %s.\n", SDL_GetError());
		return;
	}
}

/**
  * Put the given AVPacket in the given PacketQueue.
  *
  * @param	q	the queue to be used for the insert
  * @param	pkt	pkt the AVPacket to be inserted in the queue
  *
  * @return 0 if the AVPacket is correctly inserted in the given PacketQueue.
  */
int packet_queue_put(PacketQueue* q, AVPacket* pkt)
{
	AVPacketList* avPacketList;
	avPacketList = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!avPacketList)
	{
		return -1;
	}
	avPacketList->pkt = *pkt;
	avPacketList->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
	{
		q->first_pkt = avPacketList;
	}
	else
	{
		q->last_pkt->next = avPacketList;
	}
	q->last_pkt = avPacketList;
	q->nb_packets++;
	q->size += avPacketList->pkt.size;

	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);

	return 0;
}

/**
 * Get the first AVPacket from the given PacketQueue.
 *
 * @param   q       the PacketQueue to extract from
 * @param   pkt     the first AVPacket extracted from the queue
 * @param   block   0 to avoid waiting for an AVPacket to be inserted in the given
 *                  queue, != 0 otherwise.
 *
 * @return          < 0 if returning because the quit flag is set, 0 if the queue
 *                  is empty, 1 if it is not empty and a packet was extract (pkt)
 */
static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block)
{
	AVPacketList* avPacketList;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;)
	{
		if (global_video_state->quit)
		{
			ret = -1;
			break;
		}

		avPacketList = q->first_pkt;
		if (avPacketList)
		{
			q->first_pkt = avPacketList->next;
			if (!q->first_pkt)
			{
				q->last_pkt = NULL;
			}
			q->nb_packets--;
			q->size -= avPacketList->pkt.size;
			*pkt = avPacketList->pkt;
			av_free(avPacketList);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

/**
 *
 * @param queue
 */
static void packet_queue_flush(PacketQueue* queue)
{
	AVPacketList* pkt, * pkt1;

	SDL_LockMutex(queue->mutex);

	for (pkt = queue->first_pkt; pkt != NULL; pkt = pkt1)
	{
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}

	queue->last_pkt = NULL;
	queue->first_pkt = NULL;
	queue->nb_packets = 0;
	queue->size = 0;

	SDL_UnlockMutex(queue->mutex);
}

/**
 * Pull in data from audio_decode_frame(), store the result in an intermediary
 * buffer, attempt to write as many bytes as the amount defined by len to
 * stream, and get more data if we don't have enough yet, or save it for later
 * if we have some left over.
 *
 * @param   userdata    the pointer we gave to SDL.
 * @param   stream      the buffer we will be writing audio data to.
 * @param   len         the size of that buffer.
 */
void audio_callback(void* userdata, Uint8* stream, int len)
{
	VideoState* videoState = (VideoState*)userdata;

	double pts;

	while (len > 0)
	{
		if (global_video_state->quit)
		{
			return;
		}

		if (videoState->audio_buf_index >= videoState->audio_buf_size)
		{
			int audio_size = audio_decode_frame(videoState, videoState->audio_buf, sizeof(videoState->audio_buf), &pts);

			if (audio_size < 0)
			{
				videoState->audio_buf_size = 1024;

				memset(videoState->audio_buf, 0, videoState->audio_buf_size);
				printf("audio_decode_frame() failed.\n");
			}
			else
			{
				audio_size = synchronize_audio(videoState, (int16_t*)videoState->audio_buf, audio_size);

				videoState->audio_buf_size = (unsigned)audio_size;
			}

			videoState->audio_buf_index = 0;
		}

		int len1 = videoState->audio_buf_size - videoState->audio_buf_index;

		if (len1 > len)
		{
			len1 = len;
		}

		memcpy(stream, (uint8_t*)videoState->audio_buf + videoState->audio_buf_index, len1);

		len -= len1;
		stream += len1;
		videoState->audio_buf_index += len1;
	}
}

/**
 * Get a packet from the queue if available. Decode the extracted packet. Once
 * we have the frame, resample it and simply copy it to our audio buffer, making
 * sure the data_size is smaller than our audio buffer.
 *
 * @param   aCodecCtx   the audio AVCodecContext used for decoding
 * @param   audio_buf   the audio buffer to write into
 * @param   buf_size    the size of the audio buffer, 1.5 larger than the one
 *                      provided by FFmpeg
 *
 * @return              0 if everything goes well, -1 in case of error or quit
 */
int audio_decode_frame(VideoState* videoState, uint8_t* audio_buf, int buf_size, double* pts_ptr)
{
	AVPacket* avPacket = av_packet_alloc();
	if (avPacket == NULL)
	{
		printf("Could not allocate AVPacket.\n");
		return -1;
	}
	static uint8_t* audio_pkt_data = NULL;
	static int audio_pkt_size = 0;

	double pts;
	int n;

	static AVFrame* avFrame = NULL;
	avFrame = av_frame_alloc();
	if (!avFrame)
	{
		printf("Could not allocate AVFrame.\n");
		return -1;
	}

	int len1 = 0;
	int data_size = 0;

	for (;;)
	{
		if (videoState->quit)
		{
			return -1;
		}

		while (audio_pkt_size > 0)
		{
			int got_frame = 0;

			int ret = avcodec_receive_frame(videoState->audio_ctx, avFrame);
			if (ret == 0)
			{
				got_frame = 1;
			}
			if (ret == AVERROR(EAGAIN))
			{
				ret = 0;
			}
			if (ret == 0)
			{
				ret = avcodec_send_packet(videoState->audio_ctx, avPacket);
			}
			if (ret == AVERROR(EAGAIN))
			{
				ret = 0;
			}
			else if (ret < 0)
			{
				printf("avcodec_receive_frame error");
				return -1;
			}
			else
			{
				len1 = avPacket->size;
			}

			if (len1 < 0)
			{
				audio_pkt_size = 0;
				break;
			}

			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			data_size = 0;

			if (got_frame)
			{
				data_size = audio_resampling(
					videoState,
					avFrame,
					AV_SAMPLE_FMT_S16,
					audio_buf
					);

				assert(data_size <= buf_size);
			}

			if (data_size <= 0)
			{
				continue;
			}

			pts = videoState->audio_clock;
			*pts_ptr = pts;
			n = 2 * videoState->audio_ctx->channels;
			videoState->audio_clock += (double)data_size / (double)(n * videoState->audio_ctx->sample_rate);

			return data_size;
		}

		if (avPacket->data)
		{
			av_packet_unref(avPacket);
		}

		int ret = packet_queue_get(&videoState->audioq, avPacket, 1);

		if (ret < 0)
		{
			return -1;
		}

		if (avPacket->data == flush_pkt.data)
		{
			avcodec_flush_buffers(videoState->audio_ctx);

			continue;
		}

		audio_pkt_data = avPacket->data;
		audio_pkt_size = avPacket->size;

		if (avPacket->pts != AV_NOPTS_VALUE)
		{
			videoState->audio_clock = av_q2d(videoState->audio_st->time_base) * avPacket->pts;
		}
	}

	return 0;
}

/**
 * Resample the audio data retrieved using FFmpeg before playing it.
 *
 * @param   audio_decode_ctx    the audio codec context retrieved from the original AVFormatContext.
 * @param   decoded_audio_frame the decoded audio frame.
 * @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
 * @param   out_channels        audio output channels, retrieved from the original audio codec context.
 * @param   out_sample_rate     audio output sample rate, retrieved from the original audio codec context.
 * @param   out_buf             audio output buffer.
 *
 * @return                      the size of the resampled audio data.
 */
static int audio_resampling(
	VideoState* videoState,
	AVFrame* decoded_audio_frame,
	enum AVSampleFormat out_sample_fmt,
	uint8_t* out_buf)
{
	AudioResamplingState* arState = getAudioResampling(videoState->audio_ctx->channel_layout);

	if (!arState->swr_ctx)
	{
		printf("swr_alloc error.\n");
		return -1;
	}

	arState->in_channel_layout = (videoState->audio_ctx->channels ==
		av_get_channel_layout_nb_channels(videoState->audio_ctx->channel_layout)) ?
		videoState->audio_ctx->channel_layout :
		av_get_default_channel_layout(videoState->audio_ctx->channels);

	if (arState->in_channel_layout <= 0)
	{
		printf("in_channel_layout error.\n");
		return -1;
	}

	if (videoState->audio_ctx->channels == 1)
	{
		arState->out_channel_layout = AV_CH_LAYOUT_MONO;
	}
	else if (videoState->audio_ctx->channels == 2)
	{
		arState->out_channel_layout = AV_CH_LAYOUT_STEREO;
	}
	else
	{
		arState->out_channel_layout = AV_CH_LAYOUT_SURROUND;
	}

	arState->in_nb_samples = decoded_audio_frame->nb_samples;
	if (arState->in_nb_samples <= 0)
	{
		printf("in_nb_samples error.\n");
		return -1;
	}

	av_opt_set_int(
		arState->swr_ctx,
		"in_channel_layout",
		arState->in_channel_layout,
		0
		);

	av_opt_set_int(
		arState->swr_ctx,
		"in_sample_rate",
		videoState->audio_ctx->sample_rate,
		0
		);

	av_opt_set_sample_fmt(
		arState->swr_ctx,
		"in_sample_fmt",
		videoState->audio_ctx->sample_fmt,
		0
		);

	av_opt_set_int(
		arState->swr_ctx,
		"out_channel_layout",
		arState->out_channel_layout,
		0
		);

	av_opt_set_int(
		arState->swr_ctx,
		"out_sample_rate",
		videoState->audio_ctx->sample_rate,
		0
		);

	av_opt_set_sample_fmt(
		arState->swr_ctx,
		"out_sample_fmt",
		out_sample_fmt,
		0
		);

	int ret = swr_init(arState->swr_ctx);
	if (ret < 0)
	{
		printf("Failed to initialize the resampling context.\n");
		return -1;
	}

	arState->max_out_nb_samples = arState->out_nb_samples = av_rescale_rnd(
		arState->in_nb_samples,
		videoState->audio_ctx->sample_rate,
		videoState->audio_ctx->sample_rate,
		AV_ROUND_UP);

	if (arState->max_out_nb_samples <= 0)
	{
		printf("av_rescale_rnd error.\n");
		return -1;
	}

	arState->out_nb_channels = av_get_channel_layout_nb_channels(arState->out_channel_layout);

	ret = av_samples_alloc_array_and_samples(
		&arState->resampled_data,
		&arState->out_linesize,
		arState->out_nb_channels,
		arState->out_nb_samples,
		out_sample_fmt,
		0
		);

	if (ret < 0)
	{
		printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
		return -1;
	}

	arState->out_nb_samples = av_rescale_rnd(
		swr_get_delay(arState->swr_ctx, videoState->audio_ctx->sample_rate) + arState->in_nb_samples,
		videoState->audio_ctx->sample_rate,
		videoState->audio_ctx->sample_rate,
		AV_ROUND_UP
		);

	if (arState->out_nb_samples <= 0)
	{
		printf("av_rescale_rnd error\n");
		return -1;
	}

	if (arState->out_nb_samples > arState->max_out_nb_samples)
	{
		av_free(arState->resampled_data[0]);

		ret = av_samples_alloc(
			arState->resampled_data,
			&arState->out_linesize,
			arState->out_nb_channels,
			arState->out_nb_samples,
			out_sample_fmt,
			1
			);

		if (ret < 0)
		{
			printf("av_samples_alloc failed.\n");
			return -1;
		}

		arState->max_out_nb_samples = arState->out_nb_samples;
	}

	if (arState->swr_ctx)
	{
		ret = swr_convert(
			arState->swr_ctx,
			arState->resampled_data,
			arState->out_nb_samples,
			(const uint8_t**)decoded_audio_frame->data,
			decoded_audio_frame->nb_samples
			);

		if (ret < 0)
		{
			printf("swr_convert_error.\n");
			return -1;
		}

		arState->resampled_data_size = av_samples_get_buffer_size(
			&arState->out_linesize,
			arState->out_nb_channels,
			ret,
			out_sample_fmt,
			1
			);

		if (arState->resampled_data_size < 0)
		{
			printf("av_samples_get_buffer_size error.\n");
			return -1;
		}
	}
	else
	{
		printf("swr_ctx null error.\n");
		return -1;
	}

	memcpy(out_buf, arState->resampled_data[0], arState->resampled_data_size);

	if (arState->resampled_data)
	{
		av_freep(&arState->resampled_data[0]);
	}

	av_freep(&arState->resampled_data);
	arState->resampled_data = NULL;

	if (arState->swr_ctx)
	{
		swr_free(&arState->swr_ctx);
	}

	return arState->resampled_data_size;
}

AudioResamplingState* getAudioResampling(uint64_t channel_layout)
{
	AudioResamplingState* audioResampling = (AudioResamplingState*)av_mallocz(sizeof(AudioResamplingState));

	audioResampling->swr_ctx = swr_alloc();
	audioResampling->in_channel_layout = channel_layout;
	audioResampling->out_channel_layout = AV_CH_LAYOUT_STEREO;
	audioResampling->out_nb_channels = 0;
	audioResampling->out_linesize = 0;
	audioResampling->in_nb_samples = 0;
	audioResampling->out_nb_samples = 0;
	audioResampling->max_out_nb_samples = 0;
	audioResampling->resampled_data = NULL;
	audioResampling->resampled_data_size = 0;

	return audioResampling;
}

int init_swr_context()
{
	
}

/**
 *
 * @param videoState
 * @param pos
 * @param rel
 */
void stream_seek(VideoState* videoState, int64_t pos, int rel)
{
	if (!videoState->seek_req)
	{
		videoState->seek_pos = pos;
		videoState->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
		videoState->seek_req = 1;
	}
}