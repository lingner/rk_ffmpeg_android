#include "rtpfec.h"

#include "rtsp.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "rtpdec.h"

#include "avformat.h"
#include "rtpdec_formats.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/get_bits.h"
#include "libavutil/mem.h"

#if 0
#include <dlfcn.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

#define MAX_CACHE_AV_SIZE (10*1024*1024)
#define MAX_CACHE_FEC_SIZE (512*1024)

#define EMPTY_BUF_SIZE RTP_MAX_PACKET_LENGTH

const char *FEC_LIB = "/system/lib/libfec.so";


struct PayloadContext {
    AVIOContext *buf;
    uint32_t timestamp;     //Timestamp
    uint16_t rtp_begin_seq; //RTP Index for FEC(Start)
    uint16_t rtp_end_seq;   //RTP Index for FEC(End)
    uint8_t  redund_num;    //FEC NB
    uint8_t  redund_idx;    //FEC Index
    uint16_t fec_len;       //FEC LENGTH
    uint16_t rtp_len;       //RTP LENGTH
    uint16_t rsv;           //Reserved
    uint8_t  fec_flag;      //
    uint8_t  fec_pack_num;
};

static PayloadContext *rsfec_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void rsfec_free_context(PayloadContext *data)
{
    if (!data)
        return;
    if (data->buf) {
        uint8_t *p;
        avio_close_dyn_buf(data->buf, &p);
        av_free(p);
    }
    av_free(data);
}

static int rsfec_init(AVFormatContext *s, int st_index, PayloadContext *priv_data){
    if(priv_data){
        priv_data->timestamp = 0;
        priv_data->rtp_begin_seq = 0;
        priv_data->rtp_end_seq   = 0;
        priv_data->redund_num    = 0;
        priv_data->redund_idx    = 0;
        priv_data->fec_len       = 0;
        priv_data->rtp_len       = 0;
        priv_data->rsv           = 0;
        priv_data->fec_flag      = FEC_FLAG_PASS;
        priv_data->fec_pack_num  = 0;
    }
    return 0;
}

static int rsfec_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                              AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                              const uint8_t *buf, int len, int flags)
{
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //|         rtp_begin_seq           |        rtp_end_seq        |
    //| redund_num | redund_idx |           fec_len             |
    //|               rtp_len               |            rsv                 |
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
    if(len < FEC_HEADER_LEN){
        av_log(ctx, AV_LOG_ERROR, "Too short RTP FEC PAYLOAD");
        return AVERROR_INVALIDDATA;
    }
    data->rtp_begin_seq = AV_RB16(buf + 0);
    data->rtp_end_seq   = AV_RB16(buf + 2);
    data->redund_num    = buf[4];
    data->redund_idx    = buf[5];
    data->fec_len       = AV_RB16(buf + 6);
    data->rtp_len       = AV_RB16(buf + 8);
    data->rsv           = AV_RB16(buf + 10);
 
    av_log(NULL,AV_LOG_ERROR,"[RTPFEC]%32s --> begin_seq=%d; end_seq=%d; rs_num =%d,rs_index =%d; fec_len=%d; rtp_len=%d",
                       __FUNCTION__, data->rtp_begin_seq, data->rtp_end_seq, data->redund_num,data->redund_idx,data->fec_len,data->rtp_len);

    return 0;
}

/*RFC5510 Reed-Solomon Forward Error Correction (FEC) */
RTPDynamicProtocolHandler ff_rsfec_dynamic_handler = {
    .enc_name         = "RS_FEC",
    .codec_type       = AVMEDIA_TYPE_ATTACHMENT,
    .codec_id         = AV_CODEC_ID_NONE,
    .alloc            = rsfec_new_context,
    .free             = rsfec_free_context,
    .parse_packet     = rsfec_handle_packet,
    .init             = rsfec_init,
    .static_payload_id = 127,
};

/*
* parse fec url from filename
*/
int fec_parse_url(AVFormatContext *s,RTSPStream** rtsp){
    RTSPState *rt = s->priv_data;
    RTSPStream* rtsp_fec = NULL;
    
    char* target = NULL;
    char hostname[1024];
    char proto[10];
    char path[MAX_URL_SIZE];
    int port = 0;

    AVStream* st = NULL;
    
    av_log(NULL, AV_LOG_ERROR, "%s",__FUNCTION__);
    if((s == NULL) || (s->filename == NULL)){
        return -1;
    }

    target = strstr(s->filename,":");
    if(target == NULL){
        return -1;
    }

    av_url_split(proto, sizeof(proto), NULL,0,
                 hostname, sizeof(hostname), NULL,
                 path, sizeof(path), s->filename);

    av_log(s, AV_LOG_ERROR, "%s: hostname = %s",__FUNCTION__,hostname);
    av_log(s, AV_LOG_ERROR, "%s: path = %s",__FUNCTION__,path);
    av_log(s, AV_LOG_ERROR, "%s: proto = %s",__FUNCTION__,proto);

    target = strstr(path,"?ChannelFECPort=");
    if(target == NULL){
         // 四川华为平台
         port -= 1;
    }
    else{
	 // 电信规范
        sscanf(target,"?ChannelFECPort=%d",&port);
        av_log(s, AV_LOG_ERROR, "%s: port = %d",__FUNCTION__,port);
    }
    
    rtsp_fec = av_mallocz(sizeof(RTSPStream));
    if (rtsp_fec == NULL)
        return -1;
    
    dynarray_add(&rt->rtsp_streams, &rt->nb_rtsp_streams, rtsp_fec);
    
    rtsp_fec->sdp_payload_type = FEC_PLAYLOAD;
    rtsp_fec->sdp_port = port;

    ff_url_join(rtsp_fec->control_url,sizeof(rtsp_fec->control_url),proto,NULL,hostname,port,NULL);
    rtsp_fec->stream_index = rt->nb_rtsp_streams-1;
    av_log(NULL, AV_LOG_ERROR, "%s: FEC url = %s,stream_index = %d",__FUNCTION__,rtsp_fec->control_url,rtsp_fec->stream_index);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return -1;
    st->id = rt->nb_rtsp_streams - 1;
    
    *rtsp = rtsp_fec;
    return 0;
}


/*
* open fec connect
*/
int fec_connect(AVFormatContext *s,RTSPStream* rtsp_fec){
    int err = 0;
    av_log(NULL,AV_LOG_ERROR,"%s:s->filename = %s",__FUNCTION__,s->filename);

    if (!ff_network_init()){
        av_log(NULL,AV_LOG_ERROR,"%s:ff_network_init fail",__FUNCTION__);
        return AVERROR(EIO);
    }
    
    int flags = AVIO_FLAG_READ;
    av_log(NULL,AV_LOG_ERROR,"%s:url = %s",__FUNCTION__,rtsp_fec->control_url);
    if (ffurl_open(&rtsp_fec->rtp_handle, rtsp_fec->control_url, flags,
                   &s->interrupt_callback, NULL) < 0) {
        err = AVERROR_INVALIDDATA;
        av_log(NULL,AV_LOG_ERROR,"%s: ffurl_open fail",__FUNCTION__);
        goto fail;
    }
    if ((err = ff_rtsp_open_transport_ctx(s, rtsp_fec)))
        goto fail;

    av_log(NULL,AV_LOG_ERROR,"%s: OK********",__FUNCTION__);
    return 0;
fail:
    ff_network_close();
    return -1;
}


/*
* 解析fec 数据的rtp头和fec头，送入RS算法时需要将 rtp头和fec
* 去掉，送入fec裸数据才能恢复丢掉的音视频包，
* 如果带有头的数据送入RS算法，恢复的包的数据是错误的
*/
unsigned char* rtp_parse_fec_header(RTPPacket *packet, uint8_t *buf, int len){
    unsigned int ssrc;
    int payload_type, seq, ext; 
    uint32_t timestamp;
    unsigned char* fec_buffer = NULL;
    
    ext = buf[0] & 0x10;
    payload_type = buf[1] & 0x7f;
    seq  = AV_RB16(buf + 2);
    timestamp = AV_RB32(buf + 4);
    ssrc = AV_RB32(buf + 8);   

    if (buf[0] & 0x20) {
        int padding = buf[len - 1];
        if (len >= 12 + padding)
            len -= padding;
    }

    len -= 12;
    buf += 12;
	
    /* RFC 3550 Section 5.3.1 RTP Header Extension handling */
    if (ext) {
        if (len < 4){
            return NULL;
        }
        /* calculate the header extension length (stored as number of 32-bit words) */
        ext = (AV_RB16(buf + 2) + 1) << 2;
        if (len < ext){
            return NULL;
        }

	 // skip past RTP header extension
        len -= ext;
        buf += ext;
    }

    packet->fec_rtp_start  = AV_RB16(buf + 0);
    packet->fec_rtp_end    = AV_RB16(buf + 2);
    packet->fec_pkt_num = buf[4];
    packet->fec_pkt_idx    = buf[5];
    packet->fec_pkt_len   = AV_RB16(buf + 6);

//    av_log(NULL, AV_LOG_ERROR, "%s,*****fec_rtp_start= %d,fec_rtp_end = %d,fec_pkt_ num = %d",__FUNCTION__,
//		packet->fec_rtp_start,packet->fec_rtp_end,packet->fec_pkt_num);

    // skip fec header size = 12
    len -= 12;
    buf += 12;

    // 申请新的内存地址分配fec payload 数据
    fec_buffer = av_mallocz(len);
    if(fec_buffer != NULL){
	 memcpy(fec_buffer,buf,len);
    }

    return fec_buffer;
}

void rtp_cache_enqueue(RTSPState *rtsps, RTPDemuxContext *rtpctx, uint8_t *buf, int len)
{
    if((NULL==rtsps)||(NULL==rtpctx)||(NULL==buf)){
        return ;
    }
    uint16_t seq = AV_RB16(buf + 2);
    RTPPacket *cur, *prev = NULL, *packet;

    pthread_mutex_lock(&(rtsps->rtp_cache_ctx->mLock));
    if(rtpctx->payload_type == FEC_PLAYLOAD){
        cur = rtsps->rtp_cache_ctx->queue_fec;
    }else{
        cur = rtsps->rtp_cache_ctx->queue_av;
    }

    /* Find the correct place in the queue to insert the packet */
    while (cur) {
        int16_t diff = seq - cur->seq;
        if (diff < 0){
	     if(diff < -1000){
		   // rtp seq revert,insert to back
	     }else{
	           break;
	     }
        }
        prev = cur;
        cur = cur->next;
    }

    packet = av_mallocz(sizeof(*packet));
    if (!packet){
	 pthread_mutex_unlock(&(rtsps->rtp_cache_ctx->mLock));
        return;
    }
    packet->recvtime = av_gettime();
    packet->seq = seq;
    packet->len = len;
    packet->buf = buf;
    packet->next = cur;

    if(rtpctx->payload_type == FEC_PLAYLOAD){
         unsigned char* fec_playload_buffer = rtp_parse_fec_header(packet,buf,len);
	  if(fec_playload_buffer != NULL){
	  	packet->buf = fec_playload_buffer;
		av_free(buf);
	  }
         rtsps->rtp_cache_ctx->queue_len_fec++;
	  rtsps->rtp_cache_ctx->fec_queue_datasize += len;
//	  av_log(NULL, AV_LOG_ERROR, "%s,enque FEC packet,seq = %d,packet->buf = %p",__FUNCTION__,seq,packet->buf);
    }else{
         rtsps->rtp_cache_ctx->queue_len_av++;
	  rtsps->rtp_cache_ctx->av_queue_datasize += len;
//	  av_log(NULL, AV_LOG_ERROR, "%s,enque AV packet,seq = %d,size = %d,%p",__FUNCTION__,seq,rtsps->rtp_cache_ctx->queue_len_av,rtsps->rtp_cache_ctx->queue_av);
    }

    // 插入排序
    if (prev){
	 if(rtpctx->payload_type == FEC_PLAYLOAD){
//	      av_log(NULL, AV_LOG_ERROR,"%s: enque fec ,prev->seq = %d,%p",__FUNCTION__,seq,rtsps->rtp_cache_ctx->queue_fec);
        }
	 prev->next = packet;
    }
    else{
        if(rtpctx->payload_type == FEC_PLAYLOAD){
//	      av_log(NULL, AV_LOG_ERROR,"%s: enque fec to queue_fec,%p",__FUNCTION__,rtsps->rtp_cache_ctx->queue_fec);
            rtsps->rtp_cache_ctx->queue_fec = packet;
        }else{
            rtsps->rtp_cache_ctx->queue_av = packet;
        }
    }
#if 1
    /* 防止缓冲数据过多
    * 缓冲数据过多时，删除队列的第一个包
    */
    if(rtpctx->payload_type == FEC_PLAYLOAD){
        if(rtsps->rtp_cache_ctx->fec_queue_datasize > MAX_CACHE_FEC_SIZE){
	      // delete the first one
	      cur = rtsps->rtp_cache_ctx->queue_fec;
	      if(cur != NULL){
	      	    packet = cur->next;
		    av_log(NULL, AV_LOG_ERROR, "%s,cache fec too much,delete one packet seq = %d",__FUNCTION__,cur->seq);
		    rtsps->rtp_cache_ctx->queue_fec= packet;
		    rtsps->rtp_cache_ctx->fec_queue_datasize  -= cur->len;
		    av_free(cur->buf);
		    cur->buf = NULL;
		    av_free(cur);
	      }
        }
    }else{
        if(rtsps->rtp_cache_ctx->av_queue_datasize > MAX_CACHE_AV_SIZE){
	      // delete the first one
	      cur = rtsps->rtp_cache_ctx->queue_av;
	      if(cur != NULL){
	      	    packet = cur->next;
		    av_log(NULL, AV_LOG_ERROR, "%s,cache av too much,delete one packet seq = %d",__FUNCTION__,cur->seq);
		    rtsps->rtp_cache_ctx->queue_av = packet;
		    rtsps->rtp_cache_ctx->av_queue_datasize -= cur->len;
		    av_free(cur->buf);
		    cur->buf = NULL;
		    av_free(cur);
	      }
        }
    }    
#endif	
    pthread_mutex_unlock(&(rtsps->rtp_cache_ctx->mLock));
}


int fec_get_flag(AVFormatContext *fc){
    RTSPState *rt = fc->priv_data;
    RTSPStream* rtsp_fec = NULL;

    for(int index = 0; index < rt->nb_rtsp_streams; index++){
        rtsp_fec = rt->rtsp_streams[index];
        if(rtsp_fec->sdp_payload_type == FEC_PLAYLOAD){
            if(rtsp_fec->dynamic_protocol_context){
                return rtsp_fec->dynamic_protocol_context->fec_flag;
            }
        }
    }
    return FEC_FLAG_PASS;
}

int has_fec(AVFormatContext *fc){
    RTSPState *rt = fc->priv_data;
    RTSPStream* rtsp_fec = NULL;
    
    for(int index = 0; index < rt->nb_rtsp_streams; index++){
        rtsp_fec = rt->rtsp_streams[index];
        if(rtsp_fec->sdp_payload_type == FEC_PLAYLOAD){
            return 1;
        }
    }
    return 0;
}

int fec_update_range(AVFormatContext *fc, int seq_start, int seq_end, int fec_num,int pack_len){
    RTSPState *rt = fc->priv_data;
    RTSPStream* rtsp_stream = NULL;

    if((NULL!=rt)&&(NULL!=rt->rtp_cache_ctx)){
        rt->rtp_cache_ctx->fec_seq_start = seq_start;
        rt->rtp_cache_ctx->fec_seq_end   = seq_end;
        rt->rtp_cache_ctx->fec_pkt_num   = fec_num;
        rt->rtp_cache_ctx->fec_pkt_len  = pack_len;
    }
   
    return 0;
}


/*
* 在音视频队列/fec队列中插入空包
* 四川移动规范要求，当rtp包(音视频/fec)丢失时，需要
* 插入全0的空包，并将空包地址传递到RS中,RS算法将
* 恢复的AV数据写到传入的地址中
*/
uint8_t* insert_empty_pkt(RTPCacheContext* cacheCtx,int seq, int pay_load){
    int start = 0;
    int end = 0;
    int i = 0;
    RTPPacket * cur = NULL;
    RTPPacket * prev = NULL;
    RTPPacket * empty = NULL;
    int len = 0;
    
    if(cacheCtx == NULL){
	 return NULL;
    }
     
    pthread_mutex_lock(&(cacheCtx->mLock));
	if(pay_load == FEC_PLAYLOAD)
	{
		cur = cacheCtx->queue_fec;
	}
	else
	{
		cur = cacheCtx->queue_av;
	}
    
    while (cur) {
        int16_t diff = seq - cur->seq;
        if (diff < 0){
	     if(diff < -1000){
		   // rtp seq revert,insert to back
	     }else{
	           break;
	     }
        }
        prev = cur;
        cur = cur->next;
    }

    len = cacheCtx->fec_pkt_len;
    if(len == 0){
	 if(cur){
	     len = cur->len;
	 }else if(prev){
	     len = prev->len;
	 }else{
	     len = EMPTY_BUF_SIZE;
	 }
    }
    
    empty = av_mallocz(sizeof(RTPPacket));
    if (!empty){
	 pthread_mutex_unlock(&(cacheCtx->mLock));
        return NULL;
    }
    empty->recvtime = av_gettime();
    empty->seq = seq;
    empty->len = len;
    empty->buf = av_mallocz(len);
    empty->next = cur;
    empty->insert = 1;
    if (prev){
	 if(pay_load == FEC_PLAYLOAD){
	 	empty->fec_rtp_start = prev->fec_rtp_start;
		empty->fec_rtp_end = prev->fec_rtp_end;
	 	av_log(NULL,AV_LOG_ERROR,"%s, prev->seq = %d,buf address = %p",__FUNCTION__,prev->seq,empty->buf);  
	 }
        prev->next = empty;
    }
    else{
	if(pay_load == FEC_PLAYLOAD)
	{
		if(cur != NULL){
			empty->fec_rtp_start = cur->fec_rtp_start;
			empty->fec_rtp_end = cur->fec_rtp_end;
		}
		cacheCtx->queue_fec = empty;
	       av_log(NULL,AV_LOG_ERROR,"%s, insert empty fec  %p",__FUNCTION__,empty);  
	}
	else
	{
	//       av_log(NULL,AV_LOG_ERROR,"%s, insert head ,buf address = %p",__FUNCTION__,empty->buf); 
		cacheCtx->queue_av= empty;
	}
    }

    if(pay_load == FEC_PLAYLOAD){
	  cacheCtx->queue_len_fec++;
		cacheCtx->fec_queue_datasize += len;
     }else{
         cacheCtx->queue_len_av++;
	  cacheCtx->av_queue_datasize += len;
    }
	
    
    pthread_mutex_unlock(&(cacheCtx->mLock));
    return empty->buf;
}

/*
* 说明:送入RS算法前，获取音视频包的地址，并填充
*             lostmap(lostmap是对音频包是否丢失的描叙,1表示未丢失,0表示丢失)
*             lostmap的排列顺序是先放av包的信息后放fec包的信息，av和fec包
*             以序列号进行排序
*/
uint8_t** rtp_get_av_vector(RTSPState *rt, int* lost_map){
    int idx = 0; 
    int pack_num = 0;
    int pkt_num  = 0;
    uint8_t** pack_vector = NULL;
    RTPPacket * empty = NULL;
    int i = 0;
    int counter = 0;
    int start = 0;
    int end = 0;
    
    if((NULL==rt)||(NULL == rt->rtp_cache_ctx)){
        return NULL;
    }

    RTPCacheContext *cctx = rt->rtp_cache_ctx;
    start = cctx->fec_seq_start;
    end = cctx->fec_seq_end;
    
    if(cctx->fec_seq_end > cctx->fec_seq_start){
    	pack_num = cctx->fec_seq_end - cctx->fec_seq_start+1;
    }else{
       pack_num = 65535 - cctx->fec_seq_start + cctx->fec_seq_end + 2;
    }
	
    if(pack_num > 0){
        //malloc two-dimensional array
        pack_vector = (uint8_t**)av_malloc(sizeof(uint8_t*)* pack_num);
	 
        for(int i = 0; i < pack_num; i++){
            pack_vector[i] = NULL;
        }
    }else{
       av_log(NULL,AV_LOG_ERROR,"[RTPFEC]%32s --> [BAD Parameter]fec_seq_end=%d, fec_seq_start=%d",
                                 __FUNCTION__, cctx->fec_seq_end, cctx->fec_seq_start);
       return NULL;
    }

    //create av_vector  
    RTPPacket* pkt = cctx->queue_av;
    do{
        if(NULL != pkt){
	     if(cctx->fec_seq_end > cctx->fec_seq_start){
		  idx = pkt->seq - cctx->fec_seq_start;
	     }else {
	     	  if(pkt->seq >= cctx->fec_seq_start){
	             idx = pkt->seq - cctx->fec_seq_start;
	          }else if(pkt->seq < cctx->fec_seq_start){
	             idx = 65535 - cctx->fec_seq_start + pkt->seq+1;
 	         }
	     }
            
            if((idx>=0)&&(idx<pack_num)){
                //only saving pointer to vector
                pack_vector[idx] = pkt->buf;
                lost_map[idx]=1;
		  		counter ++;
            }
             
            if(idx>=pack_num){
		   		break;
            }
        }
        pkt = pkt->next;
    }while(NULL != pkt);

    pkt = cctx->queue_av; 
 //   av_log(NULL,AV_LOG_ERROR,"start = %d,end = %d",cctx->fec_seq_start,cctx->fec_seq_end);
    if(cctx->fec_seq_end > cctx->fec_seq_start){
	 for(i = cctx->fec_seq_start; i  <= cctx->fec_seq_end ; i++){
	// 	av_log(NULL,AV_LOG_ERROR,"lost_map[%d] == %d",i-cctx->fec_seq_start,lost_map[i-cctx->fec_seq_start]);
	       if(lost_map[i-cctx->fec_seq_start] == 0){
		    pack_vector[i-cctx->fec_seq_start] = insert_empty_pkt(cctx,i,0);
	       }
	 }
    }else{
	 for(i = cctx->fec_seq_start; i <= 65535; i++){
	 	idx = i-cctx->fec_seq_start;
	 	if(lost_map[idx] == 0){
		    pack_vector[idx] = insert_empty_pkt(cctx,i,0);
	       }
	 }

	 for(i = 0; i <= cctx->fec_seq_end; i++){
	 	idx = 65535 - cctx->fec_seq_start+i+1;
	 	if(lost_map[idx] == 0){
		    pack_vector[idx] = insert_empty_pkt(cctx,i,0);
	       }
	 }
    }

    return pack_vector;
}

/*
* 说明:送入RS算法前，获取fec包的地址，并填充
*             lostmap(lostmap是对音频包是否丢失的描叙,1表示未丢失,0表示丢失)
*             lostmap的排列顺序是先放av包的信息后放fec包的信息，av和fec包
*             以序列号进行排序
*/
uint8_t** rtp_get_fec_vector(RTSPState *rt, int* lost_map){
    int pkt_num  = 0;
    int idx      = 0;
    int idx_base = 0;
    int idx_old  = 0;
    uint8_t** pack_vector = NULL;
    RTPCacheContext *cctx = rt->rtp_cache_ctx;
    
    if((NULL==rt)||(NULL == cctx)){
        return NULL;
    }
    
    pkt_num = cctx->fec_pkt_num;

    if(pkt_num > 0){
        //malloc two-dimensional array
        pack_vector = (uint8_t**)av_malloc(sizeof(uint8_t*)* pkt_num);
//	 av_log(NULL,AV_LOG_ERROR,"%s pkt_num = %d,%p", __FUNCTION__, pkt_num,pack_vector);  

        for(int i = 0; i < pkt_num; i++){
            pack_vector[i] = NULL;
        }
    }else{
       av_log(NULL,AV_LOG_ERROR,"[RTPFEC]%32s --> [BAD Parameter]fec_pkt_num=%d, fec_pkt_len=%d",
                                 __FUNCTION__, cctx->fec_pkt_num, cctx->fec_pkt_len);
       return NULL;
    }

    //create av_vector
    if(cctx->fec_seq_start < cctx->fec_seq_end)
    {
    	idx_base = cctx->fec_seq_end - cctx->fec_seq_start+1;
    }
    else
    {
	idx_base = 65535 - cctx->fec_seq_start + cctx->fec_seq_end + 2;
    }
 //   av_log(NULL,AV_LOG_ERROR,"%s idx_base = %d", __FUNCTION__, idx_base);  
    RTPPacket* pkt = cctx->queue_fec;
    do{
        if(NULL != pkt){
            idx = pkt->fec_pkt_idx;
	     if(idx_old > idx){break;}
            if((idx>=0)&&(idx<pkt_num)){
                //only saving pointer to vector
                pack_vector[idx] = pkt->buf;
                lost_map[idx+idx_base]=1;
	//	   av_log(NULL,AV_LOG_ERROR,"%s,idx+idx_base = %d,idx = %d",__FUNCTION__,idx+idx_base,idx); 
                idx_old = idx;
            }
            
        }
        pkt = pkt->next;
    }while(NULL != pkt);

    pkt = cctx->queue_fec;
    int cur_seq = pkt->seq;
    int cur_index = pkt->fec_pkt_idx;
    for(int i = 0; i  < pkt_num ; i++){
         if(lost_map[idx_base+i] == 0){
             int seq = cur_seq - cur_index +i;
	      if(seq > 65535)
		{
                 seq = seq - 65535 - 1;
		}else if(seq < 0){
		    seq += 65535 + 1;
		}
	       pack_vector[i] = insert_empty_pkt(cctx,seq, FEC_PLAYLOAD);
       }
     }

    return pack_vector;
}

int init_fec(RTPCacheContext** fec_context){
    RTPCacheContext*context = (RTPCacheContext*)av_mallocz(sizeof(RTPCacheContext));
    if(context!= NULL){
         pthread_mutex_init(&(context->mLock), NULL);
	  *fec_context = context;

	  return 1;
    }
    return -1;
}

void fec_close(RTPCacheContext* context){
    RTPPacket* cur = NULL;
    context->working = 1;
    av_log(NULL,AV_LOG_ERROR,"%s ", __FUNCTION__);
    if(context->thread_status >= 0){
	 av_log(NULL,AV_LOG_ERROR,"%s join thread", __FUNCTION__);
        if (pthread_join(context->tid, NULL) != 0) {
            av_log(NULL, AV_LOG_ERROR, "%s",__FUNCTION__);
        }
        context->thread_status = -1;
    }

    pthread_mutex_lock(&(context->mLock));
    cur = context->queue_av;
    while(cur != NULL){
       RTPPacket *next = cur->next;
	av_free(cur->buf);
	av_free(cur);
	cur = next;
	context->queue_len_av--;
	av_log(NULL,AV_LOG_ERROR,"%s free av queue left size = %d", __FUNCTION__,context->queue_len_av);
    }
    context->queue_av = NULL;
    context->av_queue_datasize = 0;

    cur = context->queue_fec;
    while(cur != NULL){
       RTPPacket *next = cur->next;
	av_free(cur->buf);
	av_free(cur);
	cur = next;
	context->queue_len_fec--;
	av_log(NULL,AV_LOG_ERROR,"%s free fec queue left size = %d", __FUNCTION__,context->queue_len_av);
    }
    context->queue_fec = NULL;
    context->fec_queue_datasize = 0;

    if(context->recv_buff != NULL){
        av_free(context->recv_buff);
	 context->recv_buff = NULL;
	 av_log(NULL,AV_LOG_ERROR,"%s free recv_buff", __FUNCTION__);
    }

   pthread_mutex_unlock(&(context->mLock));
}
