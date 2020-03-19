//#pragma once
//
//#include <libavformat/avformat.h>
//#include <libswresample/swresample.h>
//#include <thread.h>
//
//static void decoder_init(Decoder* d);
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
//int get_master_sync_type(VideoState* videoState);
//
//double get_master_clock(VideoState* videoState);
//
//void check_external_clock_speed(VideoState*);
//
//void packet_queue_init(PacketQueue* q);
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