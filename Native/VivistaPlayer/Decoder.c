//#include "Decoder.h"
//
//static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = { 0 };
//
//AVPacket flush_pkt;
//
///**
// * Initialize the videostate and find the available video- and audiostreams
// *
// * @param   the filepath of the video
// *
// * @return  < 0 in case of error, 0 otherwise.
// */
//bool decoder_init(VideoState* videoState, const char* filepath)
//{
//	if (!filepath)
//	{
//		printf("Path is NULL.\n");
//		return false;
//	}
//
//	int ret, i;
//	int st_index[AVMEDIA_TYPE_NB];
//	videoState->videoStream = -1;
//	videoState->audioStream = -1;
//	double ctxDuration = 0;
//	
//	AVFormatContext* inputContext = NULL;
//	inputContext = avformat_alloc_context();
//	if (!inputContext)
//	{
//		printf("Could not allocate context.\n");
//		return false;
//	}
//
//	ret = avformat_open_input(&inputContext, videoState->filename, NULL, NULL);
//	if (ret < 0)
//	{
//		printf("Could not open file %s.\n", videoState->filename);
//		return false;
//	}
//	videoState->pFormatCtx = inputContext;
//
//	ret = avformat_find_stream_info(inputContext, NULL);
//	if (ret < 0)
//	{
//		printf("Could not find stream information %s\n", videoState->filename);
//		return false;
//	}
//
//	ctxDuration = (double)(inputContext->duration) / AV_TIME_BASE;
//
//	// Select a requested stream
//	for (i = 0; i < inputContext->nb_streams; i++) {
//		AVStream* st = inputContext->streams[i];
//		enum AVMediaType type = st->codecpar->codec_type;
//		st->discard = AVDISCARD_ALL;
//		if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
//		{
//			if (avformat_match_stream_specifier(inputContext, st, wanted_stream_spec[type]) > 0)
//			{
//				st_index[type] = i;
//			}
//		}
//	}
//	for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
//		if (wanted_stream_spec[i] && st_index[i] == -1) {
//			st_index[i] = INT_MAX;
//		}
//	}
//
//	if (videoState->video_enabled)
//	{
//		st_index[AVMEDIA_TYPE_VIDEO] = 
//			av_find_best_stream(inputContext, 
//				AVMEDIA_TYPE_VIDEO, 
//				st_index[AVMEDIA_TYPE_VIDEO], 
//				-1, NULL, 0);
//	}
//
//
//	if (videoState->audio_enabled)
//	{
//		st_index[AVMEDIA_TYPE_AUDIO] =
//			av_find_best_stream(inputContext,
//				AVMEDIA_TYPE_AUDIO,
//				st_index[AVMEDIA_TYPE_AUDIO],
//				st_index[AVMEDIA_TYPE_VIDEO]
//				-1, NULL, 0);
//	}
//
//	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
//	{
//		AVStream* st = inputContext->streams[st_index[AVMEDIA_TYPE_VIDEO]];
//		AVCodecParameters* codecpar = st->codecpar;
//		if (codecpar->width)
//		{
//			// TODO (Jeroen) Window size set
//			
//		}
//		ret = stream_component_open(videoState, st_index[AVMEDIA_TYPE_VIDEO]);
//	}
//	else
//	{
//		videoState->video_enabled = false;
//	}
//
//	if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
//		ret = stream_component_open(videoState, st_index[AVMEDIA_TYPE_AUDIO]);
//	}
//	else
//	{
//		videoState->audio_enabled = false;
//	}
//
//    av_init_packet(&flush_pkt);
//    flush_pkt.data = (uint8_t*)"FLUSH";
//
//	return true;
//}


///**
// * Retrieves the AVCodec and initializes the AVCodecContext for the given AVStream
// * index.
// *
// * @param   videoState      the global VideoState reference used to save info
// *                          related to the media being played.
// * @param   stream_index    the stream index obtained from the AVFormatContext.
// *
// * @return                  < 0 in case of error, 0 otherwise.
// */
//int stream_component_open(VideoState* videoState, int stream_index)
//{
//	AVFormatContext* inputContext = videoState->pFormatCtx;
//
//	if (stream_index < 0 || stream_index >= inputContext->nb_streams)
//	{
//		printf("Invalid stream index.");
//		return -1;
//	}
//
//	AVCodecContext* codecContext = NULL;
//	codecContext = avcodec_alloc_context3(codecContext);
//	if (!codecContext)
//	{
//		printf("Could allocate a codec context.\n");
//		return -1;
//	}
//
//	int ret = avcodec_parameters_to_context(codecContext, inputContext->streams[stream_index]->codecpar);
//	if (ret < 0)
//	{
//		printf("Could not copy codec context.\n");
//		return -1;
//	}
//
//	AVCodec* codec = NULL;
//	codec = avcodec_find_decoder(codecContext->codec_id);
//	if (codec == NULL)
//	{
//		printf("Unsupported codec.\n");
//		return -1;
//	}
//
//	switch (codecContext->codec_type)
//	{
//	case AVMEDIA_TYPE_AUDIO:
//	{
//		videoState->audioStream = stream_index;
//		videoState->audio_st = inputContext->streams[stream_index];
//		videoState->audio_ctx = codecContext;
//		videoState->audio_buf_size = 0;
//		videoState->audio_buf_index = 0;
//
//		if (avcodec_open2(codecContext, codec, NULL) < 0)
//		{
//			printf("Unsupported codec.\n");
//			return -1;
//		}
//
//		// TODO (Jeroen) Initializer SwrContext
//		init_swr_context();
//
//		memset(&videoState->audio_pkt, 0, sizeof(videoState->audio_pkt));
//
//		packet_queue_init(&videoState->audioq);
//	}
//	break;
//
//	case AVMEDIA_TYPE_VIDEO:
//	{
//		videoState->videoStream = stream_index;
//		videoState->video_st = inputContext->streams[stream_index];
//		videoState->video_ctx = codecContext;
//
//		// Don't forget to initialize the frame timer and the initial
//		// previous frame delay: 1ms = 1e-6s
//		videoState->frame_timer = (double)av_gettime() / 1000000.0;
//		videoState->frame_last_delay = 40e-3;
//		videoState->video_current_pts_time = av_gettime();
//
//		AVDictionary* autoThread = NULL;
//		av_dict_set(&autoThread, "threads", "auto", 0);
//		ret = avcodec_open2(codecContext, codec, &autoThread);
//		av_dict_free(&autoThread);
//		if (ret < 0)
//		{
//			printf("Unsupported codec.\n");
//			return -1;
//		}
//
//		videoState->sws_ctx = sws_getContext(
//			videoState->video_ctx->width,
//			videoState->video_ctx->height,
//			videoState->video_ctx->pix_fmt,
//			videoState->video_ctx->width,
//			videoState->video_ctx->height,
//			AV_PIX_FMT_YUV420P,
//			SWS_BILINEAR,
//			NULL,
//			NULL,
//			NULL
//			);
//
//		packet_queue_init(&videoState->videoq);
//	}
//	break;
//	default:
//	{
//
//	}
//	break;
//	}
//
//	return 0;
//}
//
//void enable_video(VideoState* videoState, bool enable)
//{
//	if (!videoState->video_st)
//	{
//		printf("Video stream not found. \n");
//		return;
//	}
//
//	videoState->video_enabled = enable;
//}
//
//void select_video_stream(int stream_index)
//{
//	wanted_stream_spec[AVMEDIA_TYPE_VIDEO] = stream_index;
//}
//
//void enable_audio(VideoState* videoState, bool enable)
//{
//	if (!videoState->audio_st)
//	{
//		printf("Video stream not found. \n");
//		return;
//	}
//
//	videoState->audio_enabled = enable;
//}
//
//void select_audio_stream(int stream_index)
//{
//	wanted_stream_spec[AVMEDIA_TYPE_VIDEO] = stream_index;
//}
//
//bool decode(VideoState* videoState)
//{
//	// TODO (Jeroen) Check initialized
//	AVPacket* packet = av_packet_alloc();
//	int ret;
//
//	ret = av_read_frame(videoState->pFormatCtx, &packet);
//
//	if (ret < 0)
//	{
//		return false;
//	}
//
//	if (videoState->video_enabled && packet->stream_index == videoState->video_st->index)
//	{
//		update_video_frame(videoState);
//	}
//	else if (videoState->audio_enabled && packet->stream_index == videoState->audio_st->index)
//	{
//		update_audio_frame(videoState);
//	}
//	return true;
//}
//
//double get_video_frame(unsigned char** outputY, unsigned char** outputU, unsigned char** outputV)
//{
//	int64_t timeStamp = 0;
//	double timeInSec = 0;
//
//	*outputY = frame->data[0];
//	*outputU = frame->data[1];
//	*outputV = frame->data[2];
//
//	return timeInSec;
//}
//
//void update_video_frame(VideoState* videoState)
//{
//	int isFrameAvailable = 0;
//	AVFrame* frame = av_frame_alloc();
//	if (!frame)
//	{
//		return;
//	}
//
//	// TODO take packet of queue
//
//	if (avcodec_decode_video2(videoState->video_ctx, frame, &isFrameAvailable, &mPacket) < 0) {
//		return;
//	}
//
//
//	if (isFrameAvailable) {
//		
//		updateBufferState();
//	}
//}
//
//
//void free_video_frame()
//{
//	
//}
//
//double get_audio_frame(unsigned char** outputFrame, int frameSize)
//{
//	int64_t timeStamp = 0;
//	double timeInSec = 0;
//
//	return timeInSec;
//}
//
//
//void update_audio_frame(VideoState* videoState) {
//	int isFrameAvailable = 0;
//	AVFrame* frameDecoded = av_frame_alloc();
//	if (!frameDecoded)
//	{
//		return;
//	}
//	if (avcodec_decode_audio4(videoState, frameDecoded, &isFrameAvailable, &mPacket) < 0) {
//		return;
//	}
//
//	AVFrame* frame = av_frame_alloc();
//	frame->sample_rate = frameDecoded->sample_rate;
//	frame->channel_layout = av_get_default_channel_layout(mAudioInfo.channels);
//	frame->format = AV_SAMPLE_FMT_FLT;	//	For Unity format.
//	frame->best_effort_timestamp = frameDecoded->best_effort_timestamp;
//	swr_convert_frame(videoState->swr_ctx, frame, frameDecoded);
//
//	updateBufferState();
//	av_frame_free(&frameDecoded);
//}
//
//void free_audio_frame()
//{
//
//}
//
//void seek(VideoState* videoState, double time)
//{
//	uint64_t timeStamp = (uint64_t)time * AV_TIME_BASE;
//
//}
//
//double get_video_frame(VideoState* videoState, unsigned char** outputY, unsigned char** outputU, unsigned char** outputV)
//{
//
//	VideoPicture* videoPicture;
//	videoPicture = &videoState->pictq[videoState->pictq_rindex];
//
//	// lock mutex video
//	if (videoPicture->frame)
//	{
//		*outputY = videoPicture->frame->data[0];
//		*outputU = videoPicture->frame->data[1];
//		*outputV = videoPicture->frame->data[2];
//
//		int64_t timeStamp = av_frame_get_best_effort_timestamp(videoPicture->frame);
//		double timeInSec = av_q2d(videoState->videoStream->time_base) * timeStamp;
//	}
//
//
//
//
//	return timeInSec;
//}
//
///**
//  * Initialize the given PacketQueue.
//  *
//  * @param q	the PacketQueue to be initialized.
//  */
//void packet_queue_init(PacketQueue* q) {
//	memset(q, 0, sizeof(PacketQueue));
//	q->mutex = SDL_CreateMutex();
//	if (!q->mutex)
//	{
//		printf("SDL_CreateMutex Error: %s.\n", SDL_GetError());
//		return;
//	}
//	q->cond = SDL_CreateCond();
//	if (!q->cond)
//	{
//		printf("SDL_CreateCond Error %s.\n", SDL_GetError());
//		return;
//	}
//}
//
///**
//  * Put the given AVPacket in the given PacketQueue.
//  *
//  * @param	q	the queue to be used for the insert
//  * @param	pkt	pkt the AVPacket to be inserted in the queue
//  *
//  * @return 0 if the AVPacket is correctly inserted in the given PacketQueue.
//  */
//int packet_queue_put(PacketQueue* q, AVPacket* pkt)
//{
//	AVPacketList* avPacketList;
//	avPacketList = (AVPacketList*)av_malloc(sizeof(AVPacketList));
//	if (!avPacketList)
//	{
//		return -1;
//	}
//	avPacketList->pkt = *pkt;
//	avPacketList->next = NULL;
//
//	SDL_LockMutex(q->mutex);
//
//	if (!q->last_pkt)
//	{
//		q->first_pkt = avPacketList;
//	}
//	else
//	{
//		q->last_pkt->next = avPacketList;
//	}
//	q->last_pkt = avPacketList;
//	q->nb_packets++;
//	q->size += avPacketList->pkt.size;
//
//	SDL_CondSignal(q->cond);
//	SDL_UnlockMutex(q->mutex);
//
//	return 0;
//}
//
///**
// * Get the first AVPacket from the given PacketQueue.
// *
// * @param   q       the PacketQueue to extract from
// * @param   pkt     the first AVPacket extracted from the queue
// * @param   block   0 to avoid waiting for an AVPacket to be inserted in the given
// *                  queue, != 0 otherwise.
// *
// * @return          < 0 if returning because the quit flag is set, 0 if the queue
// *                  is empty, 1 if it is not empty and a packet was extract (pkt)
// */
//static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block)
//{
//	AVPacketList* avPacketList;
//	int ret;
//
//	SDL_LockMutex(q->mutex);
//
//	for (;;)
//	{
//		if (global_video_state->quit)
//		{
//			ret = -1;
//			break;
//		}
//
//		avPacketList = q->first_pkt;
//		if (avPacketList)
//		{
//			q->first_pkt = avPacketList->next;
//			if (!q->first_pkt)
//			{
//				q->last_pkt = NULL;
//			}
//			q->nb_packets--;
//			q->size -= avPacketList->pkt.size;
//			*pkt = avPacketList->pkt;
//			av_free(avPacketList);
//			ret = 1;
//			break;
//		}
//		else if (!block)
//		{
//			ret = 0;
//			break;
//		}
//		else
//		{
//			SDL_CondWait(q->cond, q->mutex);
//		}
//	}
//	SDL_UnlockMutex(q->mutex);
//	return ret;
//}
//
///**
// *
// * @param queue
// */
//static void packet_queue_flush(PacketQueue* queue)
//{
//	AVPacketList* pkt, * pkt1;
//
//	SDL_LockMutex(queue->mutex);
//
//	for (pkt = queue->first_pkt; pkt != NULL; pkt = pkt1)
//	{
//		pkt1 = pkt->next;
//		av_packet_unref(&pkt->pkt);
//		av_freep(&pkt);
//	}
//
//	queue->last_pkt = NULL;
//	queue->first_pkt = NULL;
//	queue->nb_packets = 0;
//	queue->size = 0;
//
//	SDL_UnlockMutex(queue->mutex);
//}
//int init_swr_context()
//{
//	
//}
//
///**
// *
// * @param videoState
// * @param pos
// * @param rel
// */
//void stream_seek(VideoState* videoState, int64_t pos, int rel)
//{
//	if (!videoState->seek_req)
//	{
//		videoState->seek_pos = pos;
//		videoState->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
//		videoState->seek_req = 1;
//	}
//}