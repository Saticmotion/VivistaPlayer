extern "C"
{
	#include <stdio.h>
	#include <math.h>
	#include <assert.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/imgutils.h>
	#include <libavutil/avstring.h>
	#include <libavutil/opt.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libswresample/swresample.h>
	#include <SDL2/SDL.h>
	#include <SDL2/SDL_thread.h>
}

/**
 * SDL audio buffer size in samples.
 */
#define SDL_AUDIO_BUFFER_SIZE 1024

/**
  * Maximum number of samples per channel in an audio frame.
  */
#define MAX_AUDIO_FRAME_SIZE 192000

/**
  * Audio packets queue maximum size.
  */
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)


/**
  * Video packets queue maximum size.
  */
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

/**
  * Custom SDL_Event type.
  * Notifies the next video frame has to be displayed.
  */
#define FF_REFRESH_EVENT (SDL_USEREVENT)

/**
  * Custom SDL_Event type.
  * Notifies the program needs to quit.
  */
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

/**
  * Video Frame queue size.
  */
#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue 
{
	AVPacketList*	first_pkt;
	AVPacketList*	last_pkt;
	int				nb_packets;
	int				size;
	SDL_mutex*		mutex;
	SDL_cond*		cond;
} PacketQueue;

typedef struct VideoPicture
{
	AVFrame*	frame;
	int			width;
	int			height;
	int			allocated;
} VideoPicture;

typedef struct VideoState
{
	/**
	 * File I/O Context.
	 */
	AVFormatContext* pFormatCtx;

	/**
	 * Audio Stream.
	 */
	int                 audioStream;
	AVStream*			audio_st;
	AVCodecContext*		audio_ctx;
	PacketQueue         audioq;
	uint8_t             audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int        audio_buf_size;
	unsigned int        audio_buf_index;
	AVFrame             audio_frame;
	AVPacket            audio_pkt;
	uint8_t*			audio_pkt_data;
	int                 audio_pkt_size;

	/**
	 * Video Stream.
	 */
	int                 videoStream;
	AVStream*			video_st;
	AVCodecContext*		video_ctx;
	SDL_Texture*		texture;
	SDL_Renderer*		renderer;
	PacketQueue         videoq;
	struct SwsContext*	sws_ctx;
	VideoPicture        pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int                 pictq_size;
	int                 pictq_rindex;
	int                 pictq_windex;
	SDL_mutex*			pictq_mutex;
	SDL_cond*			pictq_cond;

	/**
	 * Threads.
	 */
	SDL_Thread* decode_tid;
	SDL_Thread* video_tid;

	/**
	 * Input file name.
	 */
	char filename[1024];

	/**
	 * Global quit flag.
	 */
	int quit;

	/**
	 * Maximum number of frames to be decoded.
	 */
	long    maxFramesToDecode;
	int     currentFrameIndex;
} VideoState;

/**
 * Global SDL_Window reference.
 */
SDL_Window* screen;

/**
 * Global SDL_Surface mutex reference.
 */
SDL_mutex* screen_mutex;

/**
 * Global VideoState reference.
 */
VideoState* global_video_state;

int decode_thread(void* arg);

int stream_component_open(
	VideoState* videoState,
	int stream_index
);

void alloc_picture(void* userdata);

int queue_picture(
	VideoState* videoState,
	AVFrame* pFrame
);

int video_thread(void* arg);

void video_refresh_timer(void* userdata);

static void schedule_refresh(
	VideoState* videoState,
	int delay
);

static Uint32 sdl_refresh_timer_cb(
	Uint32 interval,
	void* opaque
);

void video_display(VideoState* videoState);

void packet_queue_init(PacketQueue* q);

int packet_queue_put(
	PacketQueue* queue,
	AVPacket* packet
);

static int packet_queue_get(
	PacketQueue* queue,
	AVPacket* packet,
	int blocking
);

void audio_callback(
	void* userdata,
	Uint8* stream,
	int len
);

int audio_decode_frame(
	VideoState* videoState,
	uint8_t* audio_buf,
	int buf_size
);

static int audio_resampling(
	AVCodecContext* audio_decode_ctx,
	AVFrame* audio_decode_frame,
	enum AVSampleFormat out_sample_fmt,
	int out_channels,
	int out_sample_rate,
	uint8_t* out_buf
);

int main(int argc, char* argv[]) 
{
	int ret = -1;

	ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	if (ret != 0)
	{
		printf("Could not initialize SDL - %s.\n.", SDL_GetError());

		return -1;
	}

	VideoState* videoState = NULL;

	videoState = (VideoState*) av_mallocz(sizeof(VideoState));

	av_strlcpy(videoState->filename, argv[1], sizeof(videoState->filename));

	char* pEnd;
	videoState->maxFramesToDecode = strtol(argv[2], &pEnd, 10);

	videoState->pictq_mutex = SDL_CreateMutex();
	videoState->pictq_cond = SDL_CreateCond();

	schedule_refresh(videoState, 39);

	videoState->decode_tid = SDL_CreateThread(decode_thread, "Decoding Thread", videoState);

	if (!videoState->decode_tid)
	{
		printf("Could not start decoding SDL_Thread - existing.\n");

		av_free(videoState);

		return -1;
	}

	SDL_Event event;

	for (;;)
	{
		SDL_WaitEvent(&event);
		switch (event.type)
		{
			case FF_QUIT_EVENT:
			case SDL_QUIT:
			{
				videoState->quit = 1;
				SDL_Quit();
			}
				break;
			case FF_REFRESH_EVENT:
			{
				video_refresh_timer(event.user.data1);
			}
				break;
			default:
			{
			
			}
				break;
		}

		if (videoState->quit)
		{
			break;
		}
	}

	return 0;
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
	VideoState* videoState = (VideoState*) arg;

	int ret = 0;

	AVFormatContext* pFormatCtx = NULL;
	if (avformat_open_input(&pFormatCtx, videoState->filename, NULL, NULL) < 0)
	{
		printf("Could not open file %s.\n", videoState->filename);
		return -1;
	}

	AVPacket pkt1, *packet = &pkt1;

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

	for (;;)
	{
		if (videoState->quit)
		{
			break;
		}

		if (videoState->audioq.size > MAX_AUDIOQ_SIZE || videoState->videoq.size > MAX_VIDEOQ_SIZE)
		{
			SDL_Delay(10);
			continue;
		}

		if (av_read_frame(videoState->pFormatCtx, packet) < 0)
		{
			if (videoState->pFormatCtx->pb->error == 0)
			{
				SDL_Delay(100);
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
		if (1)
		{
			SDL_Event event;
			event.type = FF_QUIT_EVENT;
			event.user.data1 = videoState;

			SDL_PushEvent(&event);

			return -1;
		}
	};

	return 0;
}

/**
 * Retrieves the AVCodec and initializes the AVCodecContext for the given AVStream
 * index. For the AVMEDIA_TYPE_AUDIO codec type sets the desired audio specs,
 * opens the audio device and starts playing.
 *
 * @param   videoState      the global VideoState reference used to save info
 *                          related to the media being played.
 * @param   stream_index    the stream index obtained from the AVFormatContext.
 *
 * @return                  < 0 in case of error, 0 otherwise.
 */
int stream_component_open(VideoState* videoState, int stream_index)
{
	AVFormatContext* pFormatCtx = videoState->pFormatCtx;

	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams)
	{
		printf("Invalid stream index.");
		return -1;
	}

	AVCodec* codec = NULL;
	codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codecpar->codec_id);
	if (codec == NULL)
	{
		printf("Unsupported codec.\n");
		return -1;
	}

	AVCodecContext* codecCtx = NULL;
	codecCtx = avcodec_alloc_context3(codec);
	int ret = avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar);
	if (ret != 0)
	{
		printf("Could not copy codec context.\n");
		return -1;
	}

	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO)
	{
		SDL_AudioSpec wanted_specs;
		SDL_AudioSpec specs;

		wanted_specs.freq = codecCtx->sample_rate;
		wanted_specs.format = AUDIO_S16SYS;
		wanted_specs.channels = codecCtx->channels;
		wanted_specs.silence = 0;
		wanted_specs.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_specs.callback = audio_callback;
		wanted_specs.userdata = videoState;

		ret = SDL_OpenAudio(&wanted_specs, &specs);

		if (ret < 0)
		{
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}
	}

	if (avcodec_open2(codecCtx, codec, NULL) < 0)
	{
		printf("Unsupported codec.\n");
		return -1;
	}

	switch (codecCtx->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:
	{
		videoState->audioStream = stream_index;
		videoState->audio_st = pFormatCtx->streams[stream_index];
		videoState->audio_ctx = codecCtx;
		videoState->audio_buf_size = 0;
		videoState->audio_buf_index = 0;

		memset(&videoState->audio_pkt, 0, sizeof(videoState->audio_pkt));

		packet_queue_init(&videoState->audioq);

		SDL_PauseAudio(0);
	}
	break;

	case AVMEDIA_TYPE_VIDEO:
	{
		videoState->videoStream = stream_index;
		videoState->video_st = pFormatCtx->streams[stream_index];
		videoState->video_ctx = codecCtx;

		packet_queue_init(&videoState->videoq);

		videoState->video_tid = SDL_CreateThread(video_thread, "Video Thread", videoState);

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

		screen = SDL_CreateWindow(
			"Vivista Player",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			codecCtx->width / 2,
			codecCtx->height / 2,
			SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
		);

		if (!screen)
		{
			printf("SDL: could not create window - exiting.\n");
			return -1;
		}

		SDL_GL_SetSwapInterval(1);

		screen_mutex = SDL_CreateMutex();

		videoState->renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);

		videoState->texture = SDL_CreateTexture(
			videoState->renderer,
			SDL_PIXELFORMAT_YV12,
			SDL_TEXTUREACCESS_STREAMING,
			videoState->video_ctx->width,
			videoState->video_ctx->height
		);
	}
	break;
	default:
	{

	}
	break;
	}

	return 0;
}

/**
 * Allocates a new SDL_Overlay for the VideoPicture struct referenced by the
 * global VideoState struct reference.
 * The remaining VideoPicture struct fields are also updated.
 *
 * @param   userdata    global VideoState reference.
 */
void alloc_picture(void* userdata)
{
	VideoState* videoState = (VideoState*) userdata;

	VideoPicture* videoPicture;
	videoPicture = &videoState->pictq[videoState->pictq_windex];

	if (videoPicture->frame)
	{
		av_frame_free(&videoPicture->frame);
		av_free(videoPicture->frame);
	}

	SDL_LockMutex(screen_mutex);

	int numBytes;

	numBytes = av_image_get_buffer_size(
		AV_PIX_FMT_YUV420P,
		videoState->video_ctx->width,
		videoState->video_ctx->height,
		32
	);

	uint8_t* buffer = NULL;
	buffer = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t));

	videoPicture->frame = av_frame_alloc();
	if (videoPicture->frame == NULL)
	{
		printf("Could not allocate frame.\n");
		return;
	}

	av_image_fill_arrays(
		videoPicture->frame->data,
		videoPicture->frame->linesize,
		buffer,
		AV_PIX_FMT_YUV420P,
		videoState->video_ctx->width,
		videoState->video_ctx->height,
		32
	);

	SDL_UnlockMutex(screen_mutex);

	videoPicture->width = videoState->video_ctx->width;
	videoPicture->height = videoState->video_ctx->height;
	videoPicture->allocated = 1;
}

/**
 * Waits for space in the VideoPicture queue. Allocates a new SDL_Overlay in case
 * it is not already allocated or has a different width/height. Converts the given
 * decoded AVFrame to an AVPicture using specs supported by SDL and writes it in the
 * VideoPicture queue.
 *
 * @param   videoState  global VideoState reference.
 * @param   pFrame      AVFrame to be inserted in the VideoState->pictq (as an AVPicture).
 *
 * @return              < 0 in case the global quit flag is set, 0 otherwise.
 */
int queue_picture(VideoState* videoState,AVFrame* pFrame) 
{
	SDL_LockMutex(videoState->pictq_mutex);

	while (videoState->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !videoState->quit)
	{
		SDL_CondWait(videoState->pictq_cond, videoState->pictq_mutex);
	}

	SDL_UnlockMutex(videoState->pictq_mutex);

	if (videoState->quit)
	{
		return -1;
	}

	VideoPicture* videoPicture;
	videoPicture = &videoState->pictq[videoState->pictq_windex];

	if (!videoPicture->frame ||
		videoPicture->width != videoState->video_ctx->width ||
		videoPicture->height != videoState->video_ctx->height)
	{
		videoPicture->allocated = 0;

		alloc_picture(videoState);

		if (videoState->quit)
		{
			return -1;
		}
	}

	if (videoPicture->frame)
	{
		videoPicture->frame->pict_type = pFrame->pict_type;
		videoPicture->frame->pts = pFrame->pts;
		videoPicture->frame->pkt_dts = pFrame->pkt_dts;
		videoPicture->frame->key_frame = pFrame->key_frame;
		videoPicture->frame->coded_picture_number = pFrame->coded_picture_number;
		videoPicture->frame->display_picture_number = pFrame->display_picture_number;
		videoPicture->frame->width = pFrame->width;
		videoPicture->frame->height = pFrame->height;

		sws_scale(
			videoState->sws_ctx,
			(uint8_t const* const*)pFrame->data,
			pFrame->linesize,
			0,
			videoState->video_ctx->height,
			videoPicture->frame->data,
			videoPicture->frame->linesize
		);


		++videoState->pictq_windex;

		if (videoState->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
		{
			videoState->pictq_windex = 0;
		}

		SDL_LockMutex(videoState->pictq_mutex);

		videoState->pictq_size++;

		SDL_UnlockMutex(videoState->pictq_mutex);
	}

	return 0;
}

/**
 * This thread reads in packets from the video queue, packet_queue_get(), decodes
 * the video packets into a frame, and then calls the queue_picture() function to
 * put the processed frame into the picture queue.
 *
 * @param   arg global VideoState reference.
 *
 * @return
 */
int video_thread(void* arg)
{
	VideoState* videoState = (VideoState*) arg;

	AVPacket* packet = av_packet_alloc();
	if (packet == NULL)
	{
		printf("Could not alloc packet.\n");
		return -1;
	}

	int frameFinished;

	static AVFrame* pFrame = NULL;
	pFrame = av_frame_alloc();
	if (!pFrame)
	{
		printf("Could not allocate AVFrame.\n");
		return -1;
	}

	for (;;)
	{
		if (packet_queue_get(&videoState->videoq, packet, 1) < 0)
		{
			break;
		}

		int ret = avcodec_send_packet(videoState->video_ctx, packet);
		if (ret < 0)
		{
			printf("Error sending packet for decoding.\n");
			return -1;
		}

		while (ret >= 0)
		{
			ret = avcodec_receive_frame(videoState->video_ctx, pFrame);

			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			{
				break;
			}
			else if (ret < 0)
			{
				printf("Error while decoding.\n");
				return -1;
			}
			else
			{
				frameFinished = 1;
			}
			//TODO (Jeroen) Check
			if (frameFinished)
			{
				if (queue_picture(videoState, pFrame) < 0)
				{
					break;
				}
			}
		}

		av_packet_unref(packet);
	}

	av_frame_free(&pFrame);
	av_free(pFrame);

	return 0;
}

/**
 * Pulls from the VideoPicture queue when we have something, sets our timer for
 * when the next video frame should be shown, calls the video_display() method to
 * actually show the video on the screen, then decrements the counter on the queue,
 * and decreases its size.
 *
 * @param   userdata    SDL_UserEvent->data1;   User defined data pointer.
 */
void video_refresh_timer(void* userdata)
{
	VideoState* videoState = (VideoState*) userdata;

	VideoPicture* videoPicture;

	if (videoState->video_st)
	{
		if (videoState->pictq_size == 0)
		{
			schedule_refresh(videoState, 39);
		}
		else
		{
			videoPicture = &videoState->pictq[videoState->pictq_rindex];

			schedule_refresh(videoState, 39);

			video_display(videoState);

			if (++videoState->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
			{
				videoState->pictq_rindex = 0;
			}

			SDL_LockMutex(videoState->pictq_mutex);

			videoState->pictq_size--;

			SDL_CondSignal(videoState->pictq_cond);

			SDL_UnlockMutex(videoState->pictq_mutex);
		}
	}
	else
	{
		schedule_refresh(videoState, 39);
	}
}

/**
 * Schedules video updates - every time we call this function, it will set the
 * timer, which will trigger an event, which will have our main() function in turn
 * call a function that pulls a frame from our picture queue and displays it.
 *
 * @param   videoState  global VideoState reference.
 *
 * @param   delay       the delay, expressed in milliseconds, before display the
 *                      next video frame on the screen.
 */
static void schedule_refresh(VideoState* videoState, int delay)
{
	SDL_AddTimer(delay, sdl_refresh_timer_cb, videoState);
}

/**
 * Pushes an SDL_Event of type FF_REFRESH_EVENT to the events queue.
 *
 * @param   interval
 * @param   opaque
 *
 * @return
 */
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque)
{
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;

	SDL_PushEvent(&event);

	return 0;
}

/**
 * Retrieves the video aspect ratio first, which is just the width divided by the
 * height. Then it scales the movie to fit as big as possible in our screen
 * (SDL_Surface). Then it centers the movie, and calls SDL_DisplayYUVOverlay()
 * to update the surface, making sure we use the screen mutex to access it.
 *
 * @param   videoState  the global VideoState reference.
 */
void video_display(VideoState* videoState)
{
	VideoPicture* videoPicture;

	float aspect_ratio;

	int w, h, x, y;

	videoPicture = &videoState->pictq[videoState->pictq_rindex];

	if (videoPicture->frame)
	{
		if (videoState->video_ctx->sample_aspect_ratio.num == 0)
		{
			aspect_ratio = 0;
		}
		else
		{
			aspect_ratio = av_q2d(videoState->video_ctx->sample_aspect_ratio) * 
				videoState->video_ctx->width / videoState->video_ctx->height;
		}

		if (aspect_ratio <= 0.0)
		{
			aspect_ratio = (float)videoState->video_ctx->width /
							(float)videoState->video_ctx->height;
		}

		int screen_width;
		int screen_height;
		SDL_GetWindowSize(screen, &screen_width, &screen_height);

		h = screen_height;
		w = ((int)rint(h * aspect_ratio)) & -3;

		if (w > screen_width)
		{
			w = screen_width;

			h = ((int)rint(w / aspect_ratio)) & -3;
		}

		x = (screen_width - w);
		y = (screen_height - h);

		if (++videoState->currentFrameIndex < videoState->maxFramesToDecode)
		{
			printf(
				"Frame %c (%d) pts %d dts %d key_frame %d "
				"[coded_picture_number %d, display_picture_number %d,"
				" %dx%d\n",
				av_get_picture_type_char(videoPicture->frame->pict_type),
				videoState->video_ctx->frame_number,
				videoPicture->frame->pts,
				videoPicture->frame->pkt_dts,
				videoPicture->frame->key_frame,
				videoPicture->frame->coded_picture_number,
				videoPicture->frame->display_picture_number,
				videoPicture->frame->width,
				videoPicture->frame->height
			);

			SDL_Rect rect;
			rect.x = x;
			rect.y = y;
			rect.w = 2*w;
			rect.h = 2*h;

			SDL_LockMutex(screen_mutex);

			SDL_UpdateYUVTexture(
				videoState->texture,
				&rect,
				videoPicture->frame->data[0],
				videoPicture->frame->linesize[0],
				videoPicture->frame->data[1],
				videoPicture->frame->linesize[1],
				videoPicture->frame->data[2],
				videoPicture->frame->linesize[2]
			);

			SDL_RenderClear(videoState->renderer);

			SDL_RenderCopy(videoState->renderer, videoState->texture, NULL, NULL);

			SDL_RenderPresent(videoState->renderer);

			SDL_UnlockMutex(screen_mutex);
		}
		else
		{
			SDL_Event event;
			event.type = FF_QUIT_EVENT;
			event.user.data1 = videoState;

			SDL_PushEvent(&event);
		}
	}
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
	avPacketList = (AVPacketList*) av_malloc(sizeof(AVPacketList));
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
	VideoState* videoState = (VideoState*) userdata;

	int len1 = -1;
	unsigned int audio_size = -1;

	while (len > 0)
	{
		if (global_video_state->quit)
		{
			return;
		}

		if (videoState->audio_buf_index >= videoState->audio_buf_size)
		{
			audio_size = audio_decode_frame(videoState, videoState->audio_buf, sizeof(videoState->audio_buf));

			if (audio_size < 0)
			{
				videoState->audio_buf_size = 1024;

				memset(videoState->audio_buf, 0, videoState->audio_buf_size);
				printf("audio_decode_frame() failed.\n");
			}
			else
			{
				videoState->audio_buf_size = audio_size;
			}

			videoState->audio_buf_index = 0;
		}

		len1 = videoState->audio_buf_size - videoState->audio_buf_index;

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
int audio_decode_frame(VideoState* videoState, uint8_t* audio_buf, int buf_size)
{
	AVPacket* avPacket = av_packet_alloc();
	static uint8_t* audio_pkt_data = NULL;
	static int audio_pkt_size = 0;

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
					videoState->audio_ctx,
					avFrame,
					AV_SAMPLE_FMT_S16,
					videoState->audio_ctx->channels,
					videoState->audio_ctx->sample_rate,
					audio_buf
				);
				
				assert(data_size <= buf_size);
			}

			if (data_size <= 0)
			{
				continue;
			}

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

		audio_pkt_data = avPacket->data;
		audio_pkt_size = avPacket->size;
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
	AVCodecContext* audio_decode_ctx,
	AVFrame* decoded_audio_frame,
	enum AVSampleFormat out_sample_fmt,
	int out_channels,
	int out_sample_rate,
	uint8_t* out_buf)
{
	SwrContext* swr_ctx = NULL;
	int ret = 0;
	int64_t in_channel_layout = audio_decode_ctx->channel_layout;
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
	int out_nb_channels = 0;
	int out_linesize = 0;
	int64_t in_nb_samples = 0;
	int64_t out_nb_samples = 0;
	int max_out_nb_samples = 0;
	uint8_t** resampled_data = NULL;
	int resampled_data_size = 0;

	swr_ctx = swr_alloc();

	if (!swr_ctx)
	{
		printf("swr_alloc error.\n");
		return -1;
	}

	in_channel_layout = (audio_decode_ctx->channels ==
		av_get_channel_layout_nb_channels(audio_decode_ctx->channel_layout)) ?
		audio_decode_ctx->channel_layout :
		av_get_default_channel_layout(audio_decode_ctx->channels);

	if (in_channel_layout <= 0)
	{
		printf("in_channel_layout error.\n");
		return -1;
	}

	if (out_channels == 1)
	{
		out_channel_layout = AV_CH_LAYOUT_MONO;
	}
	else if (out_channels == 2)
	{
		out_channel_layout = AV_CH_LAYOUT_STEREO;
	}
	else
	{
		out_channel_layout = AV_CH_LAYOUT_SURROUND;
	}

	in_nb_samples = decoded_audio_frame->nb_samples;
	if (in_nb_samples <= 0)
	{
		printf("in_nb_samples error.\n");
		return -1;
	}

	av_opt_set_int(
		swr_ctx,
		"in_channel_layout",
		in_channel_layout,
		0
	);

	av_opt_set_int(
		swr_ctx,
		"in_sample_rate",
		audio_decode_ctx->sample_rate,
		0
	);

	av_opt_set_sample_fmt(
		swr_ctx,
		"in_sample_fmt",
		audio_decode_ctx->sample_fmt,
		0
	);

	av_opt_set_int(
		swr_ctx,
		"out_channel_layout",
		out_channel_layout,
		0
	);

	av_opt_set_int(
		swr_ctx,
		"out_sample_rate",
		out_sample_rate,
		0
	);

	av_opt_set_sample_fmt(
		swr_ctx,
		"out_sample_fmt",
		out_sample_fmt,
		0
	);

	ret = swr_init(swr_ctx);
	if (ret < 0)
	{
		printf("Failed to initialize the resampling context.\n");
		return -1;
	}

	max_out_nb_samples = out_nb_samples = av_rescale_rnd(
		in_nb_samples,
		out_sample_rate,
		audio_decode_ctx->sample_rate,
		AV_ROUND_UP);

	if (max_out_nb_samples <= 0)
	{
		printf("av_rescale_rnd error.\n");
		return -1;
	}

	out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

	ret = av_samples_alloc_array_and_samples(
		&resampled_data,
		&out_linesize,
		out_nb_channels,
		out_nb_samples,
		out_sample_fmt,
		0
	);

	if (ret < 0)
	{
		printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
		return -1;
	}

	out_nb_samples = av_rescale_rnd(
		swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,
		out_sample_rate,
		audio_decode_ctx->sample_rate,
		AV_ROUND_UP
	);

	if (out_nb_samples <= 0)
	{
		printf("av_rescale_rnd error\n");
		return -1;
	}

	if (out_nb_samples > max_out_nb_samples)
	{
		av_free(resampled_data[0]);

		ret = av_samples_alloc(
			resampled_data,
			&out_linesize,
			out_nb_channels,
			out_nb_samples,
			out_sample_fmt,
			1
		);

		if (ret < 0)
		{
			printf("av_samples_alloc failed.\n");
			return -1;
		}

		max_out_nb_samples = out_nb_samples;
	}

	if (swr_ctx)
	{
		ret = swr_convert(
			swr_ctx,
			resampled_data,
			out_nb_samples,
			(const uint8_t**)decoded_audio_frame->data,
			decoded_audio_frame->nb_samples
		);

		if (ret < 0)
		{
			printf("swr_convert_error.\n");
			return -1;
		}

		resampled_data_size = av_samples_get_buffer_size(
			&out_linesize,
			out_nb_channels,
			ret,
			out_sample_fmt,
			1
		);

		if (resampled_data_size < 0)
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

	memcpy(out_buf, resampled_data[0], resampled_data_size);

	if (resampled_data)
	{
		av_freep(&resampled_data[0]);
	}

	av_freep(&resampled_data);
	resampled_data = NULL;

	if (swr_ctx)
	{
		swr_free(&swr_ctx);
	}

	return resampled_data_size;
}