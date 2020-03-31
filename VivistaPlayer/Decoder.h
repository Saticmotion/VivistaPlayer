#pragma once

#include <stdbool.h>

#include <libavformat/avformat.h>
#include <libswscale\swscale.h>
#include <libswresample/swresample.h>

#define THREAD_IMPLEMENTATION
#include <thread.h>


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
 * AV sync correction is done if the clock difference is above the maximum AV sync threshold.
 */
#define AV_SYNC_THRESHOLD 0.01

/**
 * No AV sync correction is done if the clock difference is below the minimum AV sync threshold.
 */
#define AV_NOSYNC_THRESHOLD 1.0

/**
  *
  */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

  /**
   *
   */
#define AUDIO_DIFF_AVG_NB 20

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

  /**
   * Default audio video sync type.
   */
#define DEFAULT_AV_SYNC_TYPE AV_SYNC_AUDIO_MASTER

/**
 * 
 */
typedef struct PacketQueue
{
	AVPacketList*		first_pkt;
	AVPacketList*		last_pkt;
	int					nb_packets;
	int					size;
	thread_mutex_t*		mutex;
	thread_signal_t*	cond;
} PacketQueue;

// TODO (Jeroen) use for video and audio
typedef struct VideoPicture
{
	AVFrame*	frame;
   	int			width;
   	int			height;
   	int			allocated;
   	double		pts;
} VideoPicture;

// TODO (Jeroen) same as above
typedef struct VideoInfo {
	int isEnabled;
	int width;
	int height;
	double lastTime;
	double totalTime;
} VideoInfo;

typedef struct AudioInfo {
	int isEnabled;
	unsigned int channels;
	unsigned int sampleRate;
	double lastTime;
	double totalTime;
} AudioInfo;

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
	struct SwrContext*	swr_ctx;
	uint8_t             audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int        audio_buf_size;
	unsigned int        audio_buf_index;
	AVFrame             audio_frame;
	AVPacket            audio_pkt;
	uint8_t*			audio_pkt_data;
	int                 audio_pkt_size;
	double				audio_clock;
	double				audio_diff_cum;
	double				audio_diff_avg_coef;
	double				audio_diff_threshold;
	int					audio_diff_avg_count;
	bool				audio_enabled;

	/**
	 * Video Stream.
	 */
	int                 videoStream;
	AVStream*			video_st;
	AVCodecContext*		video_ctx;
	PacketQueue         videoq;
	struct SwsContext*	sws_ctx;
	double				frame_timer;
	double				frame_last_pts;
	double				frame_last_delay;
	double				video_clock;
	double				video_current_pts;
	int64_t				video_current_pts_time;
	bool				video_enabled;


	VideoPicture		pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int					pictq_size;
	int					pictq_rindex;
	int					pictq_windex;
	thread_mutex_t*		pictq_mutex;
	thread_signal_t*	pictq_signal;

	/**
	  * AV Sync.
	  */
	int     av_sync_type;
	double  external_clock;
	int64_t external_clock_time;

	/**
	  * Seeking.
	  */
	int     seek_req;
	int     seek_flags;
	int64_t seek_pos;

	/**
	 * Threads.
	 */
	thread_id_t* decode_tid;
	thread_id_t* video_tid;

	/**
	 * Input file name.
	 */
	char filename[1024];
} VideoState;

typedef struct AudioResamplingState
{
	SwrContext* swr_ctx;
	int64_t		in_channel_layout;
	uint64_t	out_channel_layout;
	int			out_nb_channels;
	int			out_linesize;
	int			in_nb_samples;
	int64_t		out_nb_samples;
	int64_t		max_out_nb_samples;
	uint8_t**	resampled_data;
	int			resampled_data_size;
} AudioResamplingState;

enum
{
	/**
	  * Sync to audio clock.
	  */
	AV_SYNC_AUDIO_MASTER,
	/**
	  * Sync to video clock.
	  */
	AV_SYNC_VIDEO_MASTER,
	 /**
		* Sync to external clock: the compute clock
		*/
	AV_SYNC_EXTERNAL_MASTER,
};

#ifdef __cplusplus
extern "C" {
#endif
	bool decoder_init(VideoState* videoState, const char* filepath);

	bool decode(VideoState* videoState);

	bool seek_stream(VideoState* videoState, double seekTime);

	bool pause_stream(VideoState* videoState, bool state);

	double get_video_frame(unsigned char** outputY, unsigned char** outputU, unsigned char** outputV);

	double get_audio_frame(unsigned char** outputFrame, int frameSize);

#ifdef __cplusplus
}
#endif
//void decoder_init(Decoder* d);
//
//int decoder_start(Decoder* d, int (*fn)(void*), const char* thread_name, void* arg);
//
//void stream_seek(VideoState* videoState, int64_t pos, int64_t rel, int seek_by_bytes);
//
//void stream_toggle_pause(VideoState videoState);
//
//void toggle_pause(VideoState* videoState);
//
//void toggle_mute(VideoState* videoState);
//
//int decode_thread(void* arg);
//
//int stream_component_open(
//	VideoState* videoState,
//	int stream_index
//);
//
//void stream_component_close(VideoState* videoState, int stream_index);
//
//int get_master_sync_type(VideoState* videoState);
//
//double get_master_clock(VideoState* videoState);
//
//void check_external_clock_speed(VideoState*);
//
//int packet_queue_init(PacketQueue* q);
//
//void packet_queue_destroy(PacketQueue* q);
//
//int packet_queue_put(
//	PacketQueue* queue,
//	AVPacket* packet
//);
//
//static int packet_queue_get(
//	PacketQueue* queue,
//	AVPacket* packet,
//	int blocking
//);
//
//static void packet_queue_flush(PacketQueue* queue);
//
//int frame_queue_init(
//	FrameQueue* f,
//	PacketQueue* pktq,
//	int max_size,
//	int keep_last
//);
//
//void frame_queue_destroy(FrameQueue* f);
//
//void frame_queue_unref_item(Frame* vp);
//
//void frame_queue_signal(FrameQueue* f);
//
//Frame* frame_queue_peek(FrameQueue* f);
//
//Frame* frame_queue_peek_next(FrameQueue* f);
//
//Frame* frame_queue_peek_last(FrameQueue* f);
//
//Frame* frame_queue_peek_writable(FrameQueue* f);
//
//Frame* frame_queue_peek_readable(FrameQueue* f);
//
//void frame_queue_push(FrameQueue* f);
//
//void frame_queue_next(FrameQueue* f);
//
//int frame_queue_nb_remaining(FrameQueue* f);
//
//int64_t frame_queue_last_pos(FrameQueue* f);
//
//VideoState* stream_open(const char* filename);
//
//void stream_close(VideoState* videoState);