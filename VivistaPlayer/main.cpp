#include <stdio.h>

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavutil/imgutils.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <SDL2/SDL.h>
	#include <SDL2/SDL_thread.h>
}

int main(int argc, char *argv[]) 
{
	int ret = -1;

	ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	if (ret != 0)
	{
		printf("Could not initialize SDL -%s\n", SDL_GetError());

		return -1;
	}

	AVFormatContext* pFormatCtx = NULL;

	// Open file
	// Only looks at header
	ret = avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);
	if(ret < 0)
	{
		printf("Could not open file %s\n", argv[1]);
		
		return -1;
	}

	// Retrieve stream information
	ret = avformat_find_stream_info(pFormatCtx, NULL);
	if (ret < 0)
	{
		printf("Could not find stream information %s\n", argv[1]);

		return -1;
	}

	av_dump_format(pFormatCtx, 0, argv[1], 0);

	int i;
	AVCodecContext* pCodecCtx = NULL;

	// Find the first video stream
	int videostream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videostream = i;
			break;
		}
	}

	if (videostream == -1)
	{
		return -1;
	}

	AVCodec* pCodec = NULL;
	pCodec = avcodec_find_decoder(pFormatCtx->streams[videostream]->codecpar->codec_id);
	if (pCodec == NULL)
	{
		printf("Unsupported codec!\n");

		return -1;
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);
	ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videostream]->codecpar);
	if (ret != 0)
	{
		printf("Could not copy codec context.\n");

		return -1;
	}

	ret = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (ret < 0)
	{
		printf("Could not open codec.\n");

		return -1;
	}

	AVFrame* pFrame = NULL;
	pFrame = av_frame_alloc();
	if (pFrame == NULL)
	{
		printf("Could not allocate frame.\n");
	
		return -1;
	}

	SDL_Window* screen = SDL_CreateWindow(
		"SDL Video Player",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		pCodecCtx->width/2,
		pCodecCtx->height/2,
		SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
	);

	if (!screen)
	{
		printf("SDL: could not set video mode -exiting.\n");

		return -1;
	}

	SDL_GL_SetSwapInterval(1);

	SDL_Renderer* renderer = NULL;

	renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);

	SDL_Texture* texture = NULL;

	texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_YV12,
		SDL_TEXTUREACCESS_STREAMING,
		pCodecCtx->width,
		pCodecCtx->height
	);

	struct SwsContext *sws_ctx = NULL;
	AVPacket *pPacket = av_packet_alloc();
	if (pPacket == NULL)
	{
		printf("Could not alloc packet.\n");

		return -1;
	}

	sws_ctx = sws_getContext(
		pCodecCtx->width,
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		AV_PIX_FMT_YUV420P,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
	);

	int numBytes;
	uint8_t* buffer = NULL;

	numBytes = av_image_get_buffer_size(
		AV_PIX_FMT_YUV420P,
		pCodecCtx->width,
		pCodecCtx->height,
		32
	);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

	SDL_Event event;

	AVFrame* pict = av_frame_alloc();

	av_image_fill_arrays(
		pict->data,
		pict->linesize,
		buffer,
		AV_PIX_FMT_YUV420P,
		pCodecCtx->width,
		pCodecCtx->height,
		32
	);

	int maxFramesToDecode;
	sscanf_s(argv[2], "%d", &maxFramesToDecode);

	i = 0;
	while (av_read_frame(pFormatCtx, pPacket) >= 0)
	{
		if (pPacket->stream_index == videostream)
		{
			ret = avcodec_send_packet(pCodecCtx, pPacket);
			if (ret < 0)
			{
				printf("Error sending packet for decoding.\n");

				return -1;
			}

			while (ret >= 0)
			{
				ret = avcodec_receive_frame(pCodecCtx, pFrame);

				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				{
					break;
				}
				else if (ret < 0)
				{
					printf("Error while decoding.\n");
					return -1;
				}

				sws_scale(
					sws_ctx,
					(uint8_t const* const*)pFrame->data,
					pFrame->linesize,
					0,
					pCodecCtx->height,
					pict->data,
					pict->linesize
				);

				if (++i <= maxFramesToDecode)
				{
					double fps = av_q2d(pFormatCtx->streams[videostream]->r_frame_rate);

					double sleep_time = 1.0 / (double)fps;

					SDL_Delay((1000 * sleep_time) - 10);

					SDL_Rect rect;
					rect.x = 0;
					rect.y = 0;
					rect.w = pCodecCtx->width;
					rect.h = pCodecCtx->height;

					printf(
						"Frame %c (%d) pts %d dts %d key_frame %d "
						"[coded_picture_number %d, display_picture_number %d,"
						" %dx%d\n",
						av_get_picture_type_char(pFrame->pict_type),
						pCodecCtx->frame_number,
						pFrame->pts,
						pFrame->pkt_dts,
						pFrame->key_frame,
						pFrame->coded_picture_number,
						pFrame->display_picture_number,
						pCodecCtx->width,
						pCodecCtx->height
					);

					SDL_UpdateYUVTexture(
						texture,
						&rect,
						pict->data[0],
						pict->linesize[0],
						pict->data[1],
						pict->linesize[1],
						pict->data[2],
						pict->linesize[2]
					);

					SDL_RenderClear(renderer);

					SDL_RenderCopy(
						renderer,
						texture,
						NULL,
						NULL
					);

					SDL_RenderPresent(renderer);
				}
				else
				{
					break;
				}
			}

			if (i > maxFramesToDecode)
			{
				break;
			}
		}

		av_packet_unref(pPacket);

		SDL_PollEvent(&event);
		switch (event.type)
		{
			case SDL_QUIT:
			{
			SDL_Quit();
			exit(0);
			}
			break;

			default:
			{

			}
			break;
		}
	}

	av_free(buffer);

	av_frame_free(&pFrame);
	av_free(pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	SDL_DestroyRenderer(renderer);
	SDL_Quit();

	return 0;
}