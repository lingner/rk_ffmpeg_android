/*RFC5510 Reed-Solomon Forward Error Correction (FEC) -->Martin.Cheng*/

#ifndef AVFORMAT_RTSP_FEC_H
#define AVFORMAT_RTSP_FEC_H

#include <stdint.h>
#include "avformat.h"
#include "rtspcodes.h"
#include "rtpdec.h"
#include "network.h"
#include "httpauth.h"

#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "rtsp.h"

#define FEC_PLAYLOAD 127

#define FEC_FLAG_PASS  0x00
#define FEC_FLAG_WAIT  0x10
#define FEC_FLAG_CHECK 0x11

#define FEC_HEADER_LEN 12

#define DEBUG_FEC      0x1
#define DEBUG_RTP_FEC  0x2
#define DEBUG_RTP      0x3

#define READ_DATA_SEQ_INVALID  -2

int init_fec(RTPCacheContext** fec_contex);
void fec_close(RTPCacheContext* context);
int fec_parse_url(AVFormatContext *s,RTSPStream** rtsp);
int fec_connect(AVFormatContext *s,RTSPStream* rtsp_fec);
int fec_get_flag(AVFormatContext *fc);
int has_fec(AVFormatContext *fc);
int get_fec_seq(PayloadContext* data,int* start,int* end);
unsigned char* rtp_parse_fec_header(RTPPacket *packet, uint8_t *buf, int len);
void rtp_cache_enqueue(RTSPState *rtsps, RTPDemuxContext *rtpctx, uint8_t *buf, int len);
int fec_update_range(AVFormatContext *fc, int seq_start, int seq_end, int fec_num,int pack_len);
uint8_t** rtp_get_av_vector(RTSPState *rt, int* lost_map);
uint8_t** rtp_get_fec_vector(RTSPState *rt, int* lost_map);
uint8_t* insert_empty_pkt(RTPCacheContext* cacheCtx,int seq,int pay_load);
int fec_reconnect(AVFormatContext *s,RTSPStream* rtsp_fec);
int isFecPacket(int rtpContex_payload,int data_payload);
int deleteFecPacket(RTPCacheContext *cache_ctx,int start,int end);
#endif
