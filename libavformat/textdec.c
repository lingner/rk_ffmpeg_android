/*
 * Text subtitle demuxer
 * Copyright (c) 2015 llc llc@rock-chips.com
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#include "avformat.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"


static int64_t last_pts = 0;
static line_num = 0;

static int text_probe(AVProbeData *p)
{
    //av_log(NULL, AV_LOG_ERROR, "text_probe in");
    unsigned char *ptr = p->buf;
    //av_log(NULL, AV_LOG_ERROR, "text_probe ptr:%s", ptr);
    int i, v, num = 0;
    char c;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */

    for (i=0; i<2; i++) {
        if ((num == i || num + 1 == i)
            && sscanf(ptr, "%*d:%*2d:%*2d%*1[,.]%3d%c", &v, &c) == 2) {
            if (c == ':') {
                return (AVPROBE_SCORE_MAX);
            }
        }
        num = atoi(ptr);
        ptr += strcspn(ptr, "\n") + 1;
    }
    //av_log(NULL, AV_LOG_ERROR, "text_probe out");
    return 0;
}

static int64_t get_pts(char **buf, int *duration, char *data)
{
    int i;
    int colon_num = 0;
    int64_t start, end;
    char c1[1024] = {0};
    char c2[1024] = {0};
    int has_pts = 0;
    
    for (i=0; i<2; i++) {
        int hh1, mm1, ss1, ms1;
        if (sscanf(*buf, "%d:%2d:%2d%*1[,.]%3d%*1[:]%[^\n]",&hh1, &mm1, &ss1, &ms1, c1) >= 4) {
            start = (hh1*3600LL + mm1*60LL + ss1) * 1000LL + ms1;
            has_pts = 1;
            *buf += strcspn(*buf, "\n") + 1;
            if (strlen(c1) != 0) {
                memcpy(data, c1, strlen(c1));
            }
            
            break;
        }
        *buf += strcspn(*buf, "\n") + 1;
    }

    for (i=0; i<2; i++) {
        int hh2, mm2, ss2, ms2;
        if (sscanf(*buf, "%d:%2d:%2d%*1[,.]%3d%*1[:]%[^\n]",&hh2, &mm2, &ss2, &ms2, c2) >= 4) {
            end = (hh2*3600LL + mm2*60LL + ss2) * 1000LL + ms2;
            *buf += strcspn(*buf, "\n") + 1;
            if (strlen(c1) == 0 && strlen(c2) != 0) {
                memcpy(data, c2, strlen(c2));
            }
            break;
        }
        *buf += strcspn(*buf, "\n") + 1;
    }

    *duration = end - start;

    if (has_pts) {
        return start;
    } else {
        return AV_NOPTS_VALUE;
    }
}


static inline int is_eol(char c)
{
    return c == '\r' || c == '\n';
}

static int text_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st) {
        av_log(NULL, AV_LOG_ERROR, "text_read_header avformat_new_stream fail");
        return AVERROR(ENOMEM);
    }
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_TEXT;
    return 0;
}

static int text_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    char buffer[2048], *ptr = buffer, *ptr2, *prt3;
    char data[1024] = {0};
    int64_t pos = avio_tell(s->pb);
    int res = AVERROR_EOF;

    do {
        ptr2 = ptr;
        ptr += ff_get_line(s->pb, ptr, sizeof(buffer)+buffer-ptr);
    } while(is_eol(*ptr2) && !url_feof(s->pb) && ptr-buffer<sizeof(buffer)-1);

    do {
        prt3 = ptr;
        ptr += ff_get_line(s->pb, ptr, sizeof(buffer)+buffer-ptr);
    } while(is_eol(*prt3) && !url_feof(s->pb) && ptr-buffer<sizeof(buffer)-1);

    


    if (buffer[0]) {
        int64_t pts;
        int duration;
        const char *end = ptr;


        ptr = buffer;

        pts = get_pts(&ptr, &duration, data);
        
        if (pts != AV_NOPTS_VALUE &&
            !(res = av_new_packet(pkt, strlen(data)))) {
            memcpy(pkt->data, data, strlen(data));
            memset(data, 0, 1024);
            pkt->flags |= AV_PKT_FLAG_KEY;
            pkt->pos = pos;
            pkt->pts = pkt->dts = pts;
            pkt->duration = duration;
        }

    }
    
    return res;
}


AVInputFormat ff_text_demuxer = {
    .name        = "text",
    .long_name   = NULL_IF_CONFIG_SMALL("Text subtitle"),
    .read_probe  = text_probe,
    .read_header = text_read_header,
    .read_packet = text_read_packet,
    .flags       = AVFMT_GENERIC_INDEX,
};

