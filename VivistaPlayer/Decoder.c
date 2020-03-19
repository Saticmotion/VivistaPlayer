//#include "Decoder.h"
//#include "packet_queue.h"
//#include "clock.h"
//
///**
//  * Maximum number of samples per channel in an audio frame.
//  */
//#define MAX_AUDIO_FRAME_SIZE 192000
//
//#define VIDEO_PICTURE_QUEUE_SIZE 3
//#define SUBPICTURE_QUEUE_SIZE 16
//#define SAMPLE_QUEUE_SIZE 9
//#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))
//
//typedef struct PacketQueue
//{
//	AVPacketList*		first_pkt;
//	AVPacketList*		last_pkt;
//	int					nb_packets;
//	int					size;
//	int64_t				duration;
//	int					serial;
//	int					abort_request;
//	thread_mutex_t*		mutex;
//	thread_signal_t*	signal;
//} PacketQueue;
//
//typedef struct Frame {
//	AVFrame*	frame;
//	int			serial;
//	double		pts;
//	double		duration;
//	int64_t		pos;
//	int			width;
//	int			height;
//	int			format;
//	AVRational	sar;
//	int			uploaded;
//} Frame;
//
//typedef struct FrameQueue
//{
//	Frame queue[FRAME_QUEUE_SIZE];
//	int rindex;
//	int windex;
//	int size;
//	int max_size;
//	int keep_last;
//	int reindex_shown;
//	thread_mutex_t* mutex;
//	thread_signal_t* signal;
//	PacketQueue* packet_q
//} FrameQueue;
//
//typedef struct Decoder {
//	AVPacket			pkt;
//	PacketQueue*		queue;
//	AVCodecContext*		av_ctx;
//	int					pkt_serial;
//	int					finished;
//	int					packet_pending;
//	int64_t				start_pts;
//	AVRational			start_pts_tb;
//	int64_t				next_pts;
//	AVRational			next_pts_tb;
//	thread_ptr_t*		decoder_tid;
//	thread_signal_t*	empty_queue_signal;
//} Decoder;
//
//typedef struct VideoState
//{
//	/**
//	 * File I/O Context.
//	 */
//	AVFormatContext* pFormatCtx;
//
//	/**
//	 * Audio Stream.
//	 */
//	int                 audioStream;
//	AVStream*			audio_st;
//	AVCodecContext*		audio_ctx;
//	PacketQueue*        audioq;
//	uint8_t             audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
//	unsigned int        audio_buf_size;
//	unsigned int        audio_buf_index;
//	AVFrame             audio_frame;
//	AVPacket            audio_pkt;
//	uint8_t*			audio_pkt_data;
//	int                 audio_pkt_size;
//	double				audio_clock;
//	int					audio_clock_serial;
//
//	/**
//	 * Video Stream.
//	 */
//	int                 videoStream;
//	AVStream*			video_st;
//	AVCodecContext*		video_ctx;
//	PacketQueue*		videoq;
//	struct SwsContext*	sws_ctx;
//	double				frame_timer;
//	double				frame_last_returned_time;
//	double				frame_last_delay;
//
//	double				audio_diff_cum;
//	double				audio_diff_avg_coef;
//	double				audio_diff_threshold;
//	int					audio_diff_avg_count;
//
//	/**
//	 * AV Sync.
//	 */
//	int     av_sync_type;
//
//	/**
//	 * Seeking.
//	 */
//	int     seek_req;
//	int     seek_flags;
//	int64_t seek_pos;
//	int64_t seek_rel;
//
//	/**
//	 * Clocks.
//	 */
//	Clock video_clock;
//	Clock audio_clock;
//	Clock extern_clock;
//
//	/**
//	 * FrameQueues.
//	 */
//	FrameQueue pictq;
//	FrameQueue sampq;
//
//	/**
//	 * Decoders.
//	 */
//	Decoder video_dec;
//	Decoder audio_dec;
//
//	/**
//	 * Threads.
//	 */
//	thread_ptr_t* decode_tid;
//	thread_ptr_t* video_tid;
//
//	/**
//	 * Input file name.
//	 */
//	char filename[1024];
//
//	int     currentFrameIndex;
//} VideoState;
//
//typedef struct AudioResamplingState
//{
//	SwrContext* swr_ctx;
//	int64_t		in_channel_layout;
//	uint64_t	out_channel_layout;
//	int			out_nb_channels;
//	int			out_linesize;
//	int			in_nb_samples;
//	int64_t		out_nb_samples;
//	int64_t		max_out_nb_samples;
//	uint8_t**	resampled_data;
//	int			resampled_data_size;
//} AudioResamplingState;
//
//
//static void decoder_init(Decoder* d)
//{
//	memset(d, 0, sizeof(Decoder));
//}
//
//static void decoder_destroy(Decoder* d)
//{
//	av_packet_unref(&d->pkt);
//	avcodec_free_context(&d->av_ctx);
//}
//
//int packet_queue_init(PacketQueue* q)
//{
//	memset(q, 0, sizeof(PacketQueue));
//	thread_mutex_init(q->mutex);
//	if (!q->mutex)
//	{
//		printf("Failed to initialize mutex.\n");
//		return AVERROR(ENOMEM);
//	}
//	thread_signal_init(q->signal);
//	if (!q->signal)
//	{
//		printf("Failed to initialize signal.\n");
//		return AVERROR(ENOMEM);
//	}
//	q->abort_request = 1;
//	return 0;
//}