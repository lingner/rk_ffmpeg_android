/*
 * H.26L/H.264/AVC/JVT/14496-10/... parameter set decoding
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * H.264 / AVC / MPEG4 part10 parameter set decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/imgutils.h"
#include "internal.h"
#include "dsputil.h"
#include "avcodec.h"
#include "h264.h"
#include "h264data.h" //FIXME FIXME FIXME (just for zigzag_scan)
#include "golomb.h"


//#undef NDEBUG
#include <assert.h>

#define AV_LOG AV_LOG_DEBUG //AV_LOG_ERROR

static const AVRational pixel_aspect[17]={
 {0, 1},
 {1, 1},
 {12, 11},
 {10, 11},
 {16, 11},
 {40, 33},
 {24, 11},
 {20, 11},
 {32, 11},
 {80, 33},
 {18, 11},
 {15, 11},
 {64, 33},
 {160,99},
 {4, 3},
 {3, 2},
 {2, 1},
};

#define QP(qP,depth) ( (qP)+6*((depth)-8) )

#define CHROMA_QP_TABLE_END(d) \
     QP(0,d),  QP(1,d),  QP(2,d),  QP(3,d),  QP(4,d),  QP(5,d),\
     QP(6,d),  QP(7,d),  QP(8,d),  QP(9,d), QP(10,d), QP(11,d),\
    QP(12,d), QP(13,d), QP(14,d), QP(15,d), QP(16,d), QP(17,d),\
    QP(18,d), QP(19,d), QP(20,d), QP(21,d), QP(22,d), QP(23,d),\
    QP(24,d), QP(25,d), QP(26,d), QP(27,d), QP(28,d), QP(29,d),\
    QP(29,d), QP(30,d), QP(31,d), QP(32,d), QP(32,d), QP(33,d),\
    QP(34,d), QP(34,d), QP(35,d), QP(35,d), QP(36,d), QP(36,d),\
    QP(37,d), QP(37,d), QP(37,d), QP(38,d), QP(38,d), QP(38,d),\
    QP(39,d), QP(39,d), QP(39,d), QP(39,d)

const uint8_t ff_h264_chroma_qp[7][QP_MAX_NUM+1] = {
    {
        CHROMA_QP_TABLE_END(8)
    },
    {
        0, 1, 2, 3, 4, 5,
        CHROMA_QP_TABLE_END(9)
    },
    {
        0, 1, 2, 3,  4,  5,
        6, 7, 8, 9, 10, 11,
        CHROMA_QP_TABLE_END(10)
    },
    {
        0,  1, 2, 3,  4,  5,
        6,  7, 8, 9, 10, 11,
        12,13,14,15, 16, 17,
        CHROMA_QP_TABLE_END(11)
    },
    {
        0,  1, 2, 3,  4,  5,
        6,  7, 8, 9, 10, 11,
        12,13,14,15, 16, 17,
        18,19,20,21, 22, 23,
        CHROMA_QP_TABLE_END(12)
    },
    {
        0,  1, 2, 3,  4,  5,
        6,  7, 8, 9, 10, 11,
        12,13,14,15, 16, 17,
        18,19,20,21, 22, 23,
        24,25,26,27, 28, 29,
        CHROMA_QP_TABLE_END(13)
    },
    {
        0,  1, 2, 3,  4,  5,
        6,  7, 8, 9, 10, 11,
        12,13,14,15, 16, 17,
        18,19,20,21, 22, 23,
        24,25,26,27, 28, 29,
        30,31,32,33, 34, 35,
        CHROMA_QP_TABLE_END(14)
    },
};

static const uint8_t default_scaling4[2][16]={
{   6,13,20,28,
   13,20,28,32,
   20,28,32,37,
   28,32,37,42
},{
   10,14,20,24,
   14,20,24,27,
   20,24,27,30,
   24,27,30,34
}};

static const uint8_t default_scaling8[2][64]={
{   6,10,13,16,18,23,25,27,
   10,11,16,18,23,25,27,29,
   13,16,18,23,25,27,29,31,
   16,18,23,25,27,29,31,33,
   18,23,25,27,29,31,33,36,
   23,25,27,29,31,33,36,38,
   25,27,29,31,33,36,38,40,
   27,29,31,33,36,38,40,42
},{
    9,13,15,17,19,21,22,24,
   13,13,17,19,21,22,24,25,
   15,17,19,21,22,24,25,27,
   17,19,21,22,24,25,27,28,
   19,21,22,24,25,27,28,30,
   21,22,24,25,27,28,30,32,
   22,24,25,27,28,30,32,33,
   24,25,27,28,30,32,33,35
}};

static inline int decode_hrd_parameters(H264Context *h, SPS *sps){
    MpegEncContext * const s = &h->s;
    int cpb_count, i;
    cpb_count = get_ue_golomb_31(&s->gb) + 1;

    if(cpb_count > 32U){
        av_log(h->s.avctx, AV_LOG_ERROR, "cpb_count %d invalid\n", cpb_count);
        return -1;
    }

    get_bits(&s->gb, 4); /* bit_rate_scale */
    get_bits(&s->gb, 4); /* cpb_size_scale */
    for(i=0; i<cpb_count; i++){
        get_ue_golomb_long(&s->gb); /* bit_rate_value_minus1 */
        get_ue_golomb_long(&s->gb); /* cpb_size_value_minus1 */
        get_bits1(&s->gb);     /* cbr_flag */
    }
    sps->initial_cpb_removal_delay_length = get_bits(&s->gb, 5) + 1;
    sps->cpb_removal_delay_length = get_bits(&s->gb, 5) + 1;
    sps->dpb_output_delay_length = get_bits(&s->gb, 5) + 1;
    sps->time_offset_length = get_bits(&s->gb, 5);
    sps->cpb_cnt = cpb_count;
    return 0;
}

static inline int decode_vui_parameters(H264Context *h, SPS *sps){
    MpegEncContext * const s = &h->s;
    int aspect_ratio_info_present_flag;
    unsigned int aspect_ratio_idc;

    aspect_ratio_info_present_flag= get_bits1(&s->gb);

    if( aspect_ratio_info_present_flag ) {
        aspect_ratio_idc= get_bits(&s->gb, 8);
        if( aspect_ratio_idc == EXTENDED_SAR ) {
            sps->sar.num= get_bits(&s->gb, 16);
            sps->sar.den= get_bits(&s->gb, 16);
        }else if(aspect_ratio_idc < FF_ARRAY_ELEMS(pixel_aspect)){
            sps->sar=  pixel_aspect[aspect_ratio_idc];
        }else{
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal aspect ratio\n");
            return -1;
        }
    }else{
        sps->sar.num=
        sps->sar.den= 0;
    }
//            s->avctx->aspect_ratio= sar_width*s->width / (float)(s->height*sar_height);

    if(get_bits1(&s->gb)){      /* overscan_info_present_flag */
        get_bits1(&s->gb);      /* overscan_appropriate_flag */
    }

    sps->video_signal_type_present_flag = get_bits1(&s->gb);
    if(sps->video_signal_type_present_flag){
        get_bits(&s->gb, 3);    /* video_format */
        sps->full_range = get_bits1(&s->gb); /* video_full_range_flag */

        sps->colour_description_present_flag = get_bits1(&s->gb);
        if(sps->colour_description_present_flag){
            sps->color_primaries = get_bits(&s->gb, 8); /* colour_primaries */
            sps->color_trc       = get_bits(&s->gb, 8); /* transfer_characteristics */
            sps->colorspace      = get_bits(&s->gb, 8); /* matrix_coefficients */
            if (sps->color_primaries >= AVCOL_PRI_NB)
                sps->color_primaries  = AVCOL_PRI_UNSPECIFIED;
            if (sps->color_trc >= AVCOL_TRC_NB)
                sps->color_trc  = AVCOL_TRC_UNSPECIFIED;
            if (sps->colorspace >= AVCOL_SPC_NB)
                sps->colorspace  = AVCOL_SPC_UNSPECIFIED;
        }
    }

    if(get_bits1(&s->gb)){      /* chroma_location_info_present_flag */
        s->avctx->chroma_sample_location = get_ue_golomb(&s->gb)+1;  /* chroma_sample_location_type_top_field */
        get_ue_golomb(&s->gb);  /* chroma_sample_location_type_bottom_field */
    }

    sps->timing_info_present_flag = get_bits1(&s->gb);
    if(sps->timing_info_present_flag){
        sps->num_units_in_tick = get_bits_long(&s->gb, 32);
        sps->time_scale = get_bits_long(&s->gb, 32);
        if(!sps->num_units_in_tick || !sps->time_scale){
            av_log(h->s.avctx, AV_LOG_ERROR, "time_scale/num_units_in_tick invalid or unsupported (%d/%d)\n", sps->time_scale, sps->num_units_in_tick);
            return -1;
        }
        sps->fixed_frame_rate_flag = get_bits1(&s->gb);
    }

    sps->nal_hrd_parameters_present_flag = get_bits1(&s->gb);
    if(sps->nal_hrd_parameters_present_flag)
        if(decode_hrd_parameters(h, sps) < 0)
            return -1;
    sps->vcl_hrd_parameters_present_flag = get_bits1(&s->gb);
    if(sps->vcl_hrd_parameters_present_flag)
        if(decode_hrd_parameters(h, sps) < 0)
            return -1;
    if(sps->nal_hrd_parameters_present_flag || sps->vcl_hrd_parameters_present_flag)
        get_bits1(&s->gb);     /* low_delay_hrd_flag */
    sps->pic_struct_present_flag = get_bits1(&s->gb);
    if(!get_bits_left(&s->gb))
        return 0;
    sps->bitstream_restriction_flag = get_bits1(&s->gb);
    if(sps->bitstream_restriction_flag){
        get_bits1(&s->gb);     /* motion_vectors_over_pic_boundaries_flag */
        get_ue_golomb(&s->gb); /* max_bytes_per_pic_denom */
        get_ue_golomb(&s->gb); /* max_bits_per_mb_denom */
        get_ue_golomb(&s->gb); /* log2_max_mv_length_horizontal */
        get_ue_golomb(&s->gb); /* log2_max_mv_length_vertical */
        sps->num_reorder_frames= get_ue_golomb(&s->gb);
        get_ue_golomb(&s->gb); /*max_dec_frame_buffering*/

        if (get_bits_left(&s->gb) < 0) {
            sps->num_reorder_frames=0;
            sps->bitstream_restriction_flag= 0;
        }

        if(sps->num_reorder_frames > 16U /*max_dec_frame_buffering || max_dec_frame_buffering > 16*/){
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal num_reorder_frames %d\n", sps->num_reorder_frames);
            return -1;
        }
    }

    if (get_bits_left(&s->gb) < 0) {
        av_log(h->s.avctx, AV_LOG_ERROR, "Overread VUI by %d bits\n", -get_bits_left(&s->gb));
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static void decode_scaling_list(H264Context *h, uint8_t *factors, int size,
                                const uint8_t *jvt_list, const uint8_t *fallback_list){
    MpegEncContext * const s = &h->s;
    int i, last = 8, next = 8;
    const uint8_t *scan = size == 16 ? zigzag_scan : ff_zigzag_direct;
    if(!get_bits1(&s->gb)) /* matrix not written, we use the predicted one */
        memcpy(factors, fallback_list, size*sizeof(uint8_t));
    else
    for(i=0;i<size;i++){
        if(next)
            next = (last + get_se_golomb(&s->gb)) & 0xff;
        if(!i && !next){ /* matrix not written, we use the preset one */
            memcpy(factors, jvt_list, size*sizeof(uint8_t));
            break;
        }
        last = factors[scan[i]] = next ? next : last;
    }
}

static void decode_scaling_matrices(H264Context *h, SPS *sps, PPS *pps, int is_sps,
                                   uint8_t (*scaling_matrix4)[16], uint8_t (*scaling_matrix8)[64]){
    MpegEncContext * const s = &h->s;
    int fallback_sps = !is_sps && sps->scaling_matrix_present;
    const uint8_t *fallback[4] = {
        fallback_sps ? sps->scaling_matrix4[0] : default_scaling4[0],
        fallback_sps ? sps->scaling_matrix4[3] : default_scaling4[1],
        fallback_sps ? sps->scaling_matrix8[0] : default_scaling8[0],
        fallback_sps ? sps->scaling_matrix8[3] : default_scaling8[1]
    };
    if(get_bits1(&s->gb)){
        sps->scaling_matrix_present |= is_sps;
        decode_scaling_list(h,scaling_matrix4[0],16,default_scaling4[0],fallback[0]); // Intra, Y
        decode_scaling_list(h,scaling_matrix4[1],16,default_scaling4[0],scaling_matrix4[0]); // Intra, Cr
        decode_scaling_list(h,scaling_matrix4[2],16,default_scaling4[0],scaling_matrix4[1]); // Intra, Cb
        decode_scaling_list(h,scaling_matrix4[3],16,default_scaling4[1],fallback[1]); // Inter, Y
        decode_scaling_list(h,scaling_matrix4[4],16,default_scaling4[1],scaling_matrix4[3]); // Inter, Cr
        decode_scaling_list(h,scaling_matrix4[5],16,default_scaling4[1],scaling_matrix4[4]); // Inter, Cb
        if(is_sps || pps->transform_8x8_mode){
            decode_scaling_list(h,scaling_matrix8[0],64,default_scaling8[0],fallback[2]);  // Intra, Y
            decode_scaling_list(h,scaling_matrix8[3],64,default_scaling8[1],fallback[3]);  // Inter, Y
            if(sps->chroma_format_idc == 3){
                decode_scaling_list(h,scaling_matrix8[1],64,default_scaling8[0],scaling_matrix8[0]);  // Intra, Cr
                decode_scaling_list(h,scaling_matrix8[4],64,default_scaling8[1],scaling_matrix8[3]);  // Inter, Cr
                decode_scaling_list(h,scaling_matrix8[2],64,default_scaling8[0],scaling_matrix8[1]);  // Intra, Cb
                decode_scaling_list(h,scaling_matrix8[5],64,default_scaling8[1],scaling_matrix8[4]);  // Inter, Cb
            }
        }
    }
}

int ff_h264_decode_seq_parameter_set(H264Context *h){
    MpegEncContext * const s = &h->s;
    int profile_idc, level_idc, constraint_set_flags = 0;
    unsigned int sps_id;
    int i;
    SPS *sps;
    SPS_EXT* sps_ext = NULL;

    profile_idc= get_bits(&s->gb, 8);
    constraint_set_flags |= get_bits1(&s->gb) << 0;   //constraint_set0_flag
    constraint_set_flags |= get_bits1(&s->gb) << 1;   //constraint_set1_flag
    constraint_set_flags |= get_bits1(&s->gb) << 2;   //constraint_set2_flag
    constraint_set_flags |= get_bits1(&s->gb) << 3;   //constraint_set3_flag
    constraint_set_flags |= get_bits1(&s->gb) << 4;   //constraint_set4_flag
    constraint_set_flags |= get_bits1(&s->gb) << 5;   //constraint_set5_flag
    get_bits(&s->gb, 2); // reserved
    level_idc= get_bits(&s->gb, 8);
    sps_id= get_ue_golomb_31(&s->gb);

    if(sps_id >= MAX_SPS_COUNT) {
        av_log(h->s.avctx, AV_LOG_ERROR, "sps_id (%d) out of range\n", sps_id);
        return -1;
    }
    sps= av_mallocz(sizeof(SPS));
    if(sps == NULL)
        return -1;

    sps->time_offset_length = 24;
    sps->profile_idc= profile_idc;
    sps->constraint_set_flags = constraint_set_flags;
    sps->level_idc= level_idc;
    sps->full_range = -1;

    memset(sps->scaling_matrix4, 16, sizeof(sps->scaling_matrix4));
    memset(sps->scaling_matrix8, 16, sizeof(sps->scaling_matrix8));
    sps->scaling_matrix_present = 0;
    sps->colorspace = 2; //AVCOL_SPC_UNSPECIFIED

    if(sps->profile_idc == 100 || sps->profile_idc == 110 ||
       sps->profile_idc == 122 || sps->profile_idc == 244 || sps->profile_idc ==  44 ||
       sps->profile_idc ==  83 || sps->profile_idc ==  86 || sps->profile_idc == 118 ||
       sps->profile_idc == 128 || sps->profile_idc == 144) {
        sps->chroma_format_idc= get_ue_golomb_31(&s->gb);
        if (sps->chroma_format_idc > 3U) {
            av_log(h->s.avctx, AV_LOG_ERROR, "chroma_format_idc %d is illegal\n", sps->chroma_format_idc);
            goto fail;
        } else if(sps->chroma_format_idc == 3) {
            sps->residual_color_transform_flag = get_bits1(&s->gb);
            if(sps->residual_color_transform_flag) {
                av_log(h->s.avctx, AV_LOG_ERROR, "separate color planes are not supported\n");
                goto fail;
            }
        }
        sps->bit_depth_luma   = get_ue_golomb(&s->gb) + 8;
        sps->bit_depth_chroma = get_ue_golomb(&s->gb) + 8;
        if (sps->bit_depth_luma > 14U || sps->bit_depth_chroma > 14U) {
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal bit depth value (%d, %d)\n",
                   sps->bit_depth_luma, sps->bit_depth_chroma);
            goto fail;
        }
        sps->transform_bypass = get_bits1(&s->gb);
        decode_scaling_matrices(h, sps, NULL, 1, sps->scaling_matrix4, sps->scaling_matrix8);
    }else{
        sps->chroma_format_idc= 1;
        sps->bit_depth_luma   = 8;
        sps->bit_depth_chroma = 8;
    }

    sps->log2_max_frame_num= get_ue_golomb(&s->gb) + 4;
    if (sps->log2_max_frame_num < 4 || sps->log2_max_frame_num > 16) {
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal log2_max_frame_num %d\n",
               sps->log2_max_frame_num);
        goto fail;
    }

    sps->poc_type= get_ue_golomb_31(&s->gb);

    if(sps->poc_type == 0){ //FIXME #define
        unsigned t = get_ue_golomb(&s->gb);
        if(t>12){
            av_log(h->s.avctx, AV_LOG_ERROR, "log2_max_poc_lsb (%d) is out of range\n", t);
            goto fail;
        }
        sps->log2_max_poc_lsb= t + 4;
    } else if(sps->poc_type == 1){//FIXME #define
        sps->delta_pic_order_always_zero_flag= get_bits1(&s->gb);
        sps->offset_for_non_ref_pic= get_se_golomb(&s->gb);
        sps->offset_for_top_to_bottom_field= get_se_golomb(&s->gb);
        sps->poc_cycle_length                = get_ue_golomb(&s->gb);

        if((unsigned)sps->poc_cycle_length >= FF_ARRAY_ELEMS(sps->offset_for_ref_frame)){
            av_log(h->s.avctx, AV_LOG_ERROR, "poc_cycle_length overflow %u\n", sps->poc_cycle_length);
            goto fail;
        }

        for(i=0; i<sps->poc_cycle_length; i++)
            sps->offset_for_ref_frame[i]= get_se_golomb(&s->gb);
    }else if(sps->poc_type != 2){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal POC type %d\n", sps->poc_type);
        goto fail;
    }

    sps->ref_frame_count= get_ue_golomb_31(&s->gb);
    if(sps->ref_frame_count > MAX_PICTURE_COUNT-2 || sps->ref_frame_count > 16U){
        av_log(h->s.avctx, AV_LOG_ERROR, "too many reference frames\n");
        goto fail;
    }
    sps->gaps_in_frame_num_allowed_flag= get_bits1(&s->gb);
    sps->mb_width = get_ue_golomb(&s->gb) + 1;
    sps->mb_height= get_ue_golomb(&s->gb) + 1;
    if((unsigned)sps->mb_width >= INT_MAX/16 || (unsigned)sps->mb_height >= INT_MAX/16 ||
       av_image_check_size(16*sps->mb_width, 16*sps->mb_height, 0, h->s.avctx)){
        av_log(h->s.avctx, AV_LOG_ERROR, "mb_width/height overflow\n");
        goto fail;
    }

    sps->frame_mbs_only_flag= get_bits1(&s->gb);
    if(!sps->frame_mbs_only_flag)
        sps->mb_aff= get_bits1(&s->gb);
    else
        sps->mb_aff= 0;

    sps->direct_8x8_inference_flag= get_bits1(&s->gb);

#ifndef ALLOW_INTERLACE
    if(sps->mb_aff)
        av_log(h->s.avctx, AV_LOG_ERROR, "MBAFF support not included; enable it at compile-time.\n");
#endif
    sps->crop= get_bits1(&s->gb);
    if(sps->crop){
        int crop_vertical_limit   = sps->chroma_format_idc  & 2 ? 16 : 8;
        int crop_horizontal_limit = sps->chroma_format_idc == 3 ? 16 : 8;
        sps->crop_left  = get_ue_golomb(&s->gb);
        sps->crop_right = get_ue_golomb(&s->gb);
        sps->crop_top   = get_ue_golomb(&s->gb);
        sps->crop_bottom= get_ue_golomb(&s->gb);
        if(sps->crop_left || sps->crop_top){
            av_log(h->s.avctx, AV_LOG_ERROR, "insane cropping not completely supported, this could look slightly wrong ... (left: %d, top: %d)\n", sps->crop_left, sps->crop_top);
        }
        if(sps->crop_right >= crop_horizontal_limit || sps->crop_bottom >= crop_vertical_limit){
            av_log(h->s.avctx, AV_LOG_ERROR, "brainfart cropping not supported, cropping disabled (right: %d, bottom: %d)\n", sps->crop_right, sps->crop_bottom);
        /* It is very unlikely that partial cropping will make anybody happy.
         * Not cropping at all fixes for example playback of Sisvel 3D streams
         * in applications supporting Sisvel 3D. */
        sps->crop_left  =
        sps->crop_right =
        sps->crop_top   =
        sps->crop_bottom= 0;
        }
    }else{
        sps->crop_left  =
        sps->crop_right =
        sps->crop_top   =
        sps->crop_bottom= 0;
    }

    sps->vui_parameters_present_flag= get_bits1(&s->gb);
    if( sps->vui_parameters_present_flag )
        if (decode_vui_parameters(h, sps) < 0)
            goto fail;

    if(!sps->sar.den)
        sps->sar.den= 1;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        static const char csp[4][5] = { "Gray", "420", "422", "444" };
        av_log(h->s.avctx, AV_LOG_DEBUG, "sps:%u profile:%d/%d poc:%d ref:%d %dx%d %s %s crop:%d/%d/%d/%d %s %s %d/%d b%d reo:%d\n",
               sps_id, sps->profile_idc, sps->level_idc,
               sps->poc_type,
               sps->ref_frame_count,
               sps->mb_width, sps->mb_height,
               sps->frame_mbs_only_flag ? "FRM" : (sps->mb_aff ? "MB-AFF" : "PIC-AFF"),
               sps->direct_8x8_inference_flag ? "8B8" : "",
               sps->crop_left, sps->crop_right,
               sps->crop_top, sps->crop_bottom,
               sps->vui_parameters_present_flag ? "VUI" : "",
               csp[sps->chroma_format_idc],
               sps->timing_info_present_flag ? sps->num_units_in_tick : 0,
               sps->timing_info_present_flag ? sps->time_scale : 0,
               sps->bit_depth_luma,
               h->sps.bitstream_restriction_flag ? sps->num_reorder_frames : -1
               );
    }

    av_free(h->sps_buffers[sps_id]);
    h->sps_buffers[sps_id]= sps;
    h->sps = *sps;
    
    switch (h->sps.bit_depth_luma) {
    case 9:
 	   if (CHROMA444) {
 		   if (s->avctx->colorspace == AVCOL_SPC_RGB) {
 			   s->avctx->pix_fmt = AV_PIX_FMT_GBRP9;
 		   } else
 			   s->avctx->pix_fmt = AV_PIX_FMT_YUV444P9;
 	   } else if (CHROMA422)
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV422P9;
 	   else
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV420P9;
 	   break;
    case 10:
 	   if (CHROMA444) {
 		   if (s->avctx->colorspace == AVCOL_SPC_RGB) {
 			   s->avctx->pix_fmt = AV_PIX_FMT_GBRP10;
 		   } else
 			   s->avctx->pix_fmt = AV_PIX_FMT_YUV444P10;
 	   } else if (CHROMA422)
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
 	   else
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV420P10;
 	   break;
    case 12:
 	   if (CHROMA444) {
 		   if (s->avctx->colorspace == AVCOL_SPC_RGB) {
 			   s->avctx->pix_fmt = AV_PIX_FMT_GBRP12;
 		   } else
 			   s->avctx->pix_fmt = AV_PIX_FMT_YUV444P12;
 	   } else if (CHROMA422)
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV422P12;
 	   else
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV420P12;
 	   break;
    case 14:
 	   if (CHROMA444) {
 		   if (s->avctx->colorspace == AVCOL_SPC_RGB) {
 			   s->avctx->pix_fmt = AV_PIX_FMT_GBRP14;
 		   } else
 			   s->avctx->pix_fmt = AV_PIX_FMT_YUV444P14;
 	   } else if (CHROMA422)
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV422P14;
 	   else
 		   s->avctx->pix_fmt = AV_PIX_FMT_YUV420P14;
 	   break;
    case 8:
 	   if (CHROMA444) {
		   s->avctx->pix_fmt = s->avctx->color_range == AVCOL_RANGE_JPEG ? AV_PIX_FMT_YUVJ444P: AV_PIX_FMT_YUV444P;
		   if (s->avctx->colorspace == AVCOL_SPC_RGB) {
			   s->avctx->pix_fmt = AV_PIX_FMT_GBR24P;
			   av_log(h->s.avctx, AV_LOG_DEBUG, "Detected GBR colorspace.\n");
		   } else if (s->avctx->colorspace == AVCOL_SPC_YCGCO) {
			   av_log(h->s.avctx, AV_LOG_WARNING, "Detected unsupported YCgCo colorspace.\n");
		   }
 	   } else if (CHROMA422) {
 		   s->avctx->pix_fmt = s->avctx->color_range == AVCOL_RANGE_JPEG ? AV_PIX_FMT_YUVJ422P: AV_PIX_FMT_YUV422P;
 	   } else {
 	       const enum AVPixelFormat hwaccel_pixfmt_list_h264_jpeg_420[] = { AV_PIX_FMT_DXVA2_VLD,
                                                                            AV_PIX_FMT_VAAPI_VLD,
                                                                            AV_PIX_FMT_VDA_VLD,
                                                                            AV_PIX_FMT_YUVJ420P,
                                                                            AV_PIX_FMT_NONE
                                                                          };
		   if (s->avctx->codec)
 	           s->avctx->pix_fmt = s->avctx->get_format(s->avctx,
                                                        s->avctx->codec->pix_fmts ?
                                                        s->avctx->codec->pix_fmts :
                                                        s->avctx->color_range == AVCOL_RANGE_JPEG ?
                                                        hwaccel_pixfmt_list_h264_jpeg_420 :
                                                        ff_hwaccel_pixfmt_list_420);
 	   }
 	   break;
    default:
 	   av_log(s->avctx, AV_LOG_ERROR, "Unsupported bit depth: %d\n", h->sps.bit_depth_luma);
    }
    
    
    return 0;
fail:
    av_free(sps);
    return -1;
}

static void
build_qp_table(PPS *pps, int t, int index, const int depth)
{
    int i;
    const int max_qp = 51 + 6*(depth-8);
    for(i = 0; i < max_qp+1; i++)
        pps->chroma_qp_table[t][i] = ff_h264_chroma_qp[depth-8][av_clip(i + index, 0, max_qp)];
}

static int more_rbsp_data_in_pps(H264Context *h, PPS *pps)
{
    const SPS *sps = h->sps_buffers[pps->sps_id];
    int profile_idc = sps->profile_idc;

    if ((profile_idc == 66 || profile_idc == 77 ||
         profile_idc == 88) && (sps->constraint_set_flags & 7)) {
        av_log(h->s.avctx, AV_LOG_VERBOSE,
               "Current profile doesn't provide more RBSP data in PPS, skipping\n");
        return 0;
    }

    return 1;
}

int ff_h264_decode_picture_parameter_set(H264Context *h, int bit_length){
    MpegEncContext * const s = &h->s;
    unsigned int pps_id= get_ue_golomb(&s->gb);
    PPS *pps;
    const int qp_bd_offset = 6*(h->sps.bit_depth_luma-8);
    int bits_left;

    if(pps_id >= MAX_PPS_COUNT) {
        av_log(h->s.avctx, AV_LOG_ERROR, "pps_id (%d) out of range\n", pps_id);
        return -1;
    } else if (h->sps.bit_depth_luma > 14) {
        av_log(h->s.avctx, AV_LOG_ERROR, "Invalid luma bit depth=%d\n", h->sps.bit_depth_luma);
        return AVERROR_INVALIDDATA;
    } else if (h->sps.bit_depth_luma == 11 || h->sps.bit_depth_luma == 13) {
        av_log(h->s.avctx, AV_LOG_ERROR, "Unimplemented luma bit depth=%d\n", h->sps.bit_depth_luma);
        return AVERROR_PATCHWELCOME;
    }

    pps= av_mallocz(sizeof(PPS));
    if(pps == NULL)
        return -1;
    pps->sps_id= get_ue_golomb_31(&s->gb);
    if((unsigned)pps->sps_id>=MAX_SPS_COUNT || h->sps_buffers[pps->sps_id] == NULL){
        av_log(h->s.avctx, AV_LOG_ERROR, "sps_id out of range\n");
        goto fail;
    }

    pps->cabac= get_bits1(&s->gb);
    pps->pic_order_present= get_bits1(&s->gb);
    pps->slice_group_count= get_ue_golomb(&s->gb) + 1;
    if(pps->slice_group_count > 1 ){
        pps->mb_slice_group_map_type= get_ue_golomb(&s->gb);
        av_log(h->s.avctx, AV_LOG_ERROR, "FMO not supported\n");
        switch(pps->mb_slice_group_map_type){
        case 0:
#if 0
|   for( i = 0; i <= num_slice_groups_minus1; i++ ) |   |        |
|    run_length[ i ]                                |1  |ue(v)   |
#endif
            break;
        case 2:
#if 0
|   for( i = 0; i < num_slice_groups_minus1; i++ )  |   |        |
|{                                                  |   |        |
|    top_left_mb[ i ]                               |1  |ue(v)   |
|    bottom_right_mb[ i ]                           |1  |ue(v)   |
|   }                                               |   |        |
#endif
            break;
        case 3:
        case 4:
        case 5:
#if 0
|   slice_group_change_direction_flag               |1  |u(1)    |
|   slice_group_change_rate_minus1                  |1  |ue(v)   |
#endif
            break;
        case 6:
#if 0
|   slice_group_id_cnt_minus1                       |1  |ue(v)   |
|   for( i = 0; i <= slice_group_id_cnt_minus1; i++ |   |        |
|)                                                  |   |        |
|    slice_group_id[ i ]                            |1  |u(v)    |
#endif
            break;
        }
    }
    pps->ref_count[0]= get_ue_golomb(&s->gb) + 1;
    pps->ref_count[1]= get_ue_golomb(&s->gb) + 1;
    if(pps->ref_count[0]-1 > 32-1 || pps->ref_count[1]-1 > 32-1){
        av_log(h->s.avctx, AV_LOG_ERROR, "reference overflow (pps)\n");
        goto fail;
    }

    pps->weighted_pred= get_bits1(&s->gb);
    pps->weighted_bipred_idc= get_bits(&s->gb, 2);
    pps->init_qp= get_se_golomb(&s->gb) + 26 + qp_bd_offset;
    pps->init_qs= get_se_golomb(&s->gb) + 26 + qp_bd_offset;
    pps->chroma_qp_index_offset[0]= get_se_golomb(&s->gb);
    pps->deblocking_filter_parameters_present= get_bits1(&s->gb);
    pps->constrained_intra_pred= get_bits1(&s->gb);
    pps->redundant_pic_cnt_present = get_bits1(&s->gb);

    pps->transform_8x8_mode= 0;
    h->dequant_coeff_pps= -1; //contents of sps/pps can change even if id doesn't, so reinit
    memcpy(pps->scaling_matrix4, h->sps_buffers[pps->sps_id]->scaling_matrix4, sizeof(pps->scaling_matrix4));
    memcpy(pps->scaling_matrix8, h->sps_buffers[pps->sps_id]->scaling_matrix8, sizeof(pps->scaling_matrix8));

    bits_left = bit_length - get_bits_count(&s->gb);
    if(bits_left > 0 && more_rbsp_data_in_pps(h, pps)){
        pps->transform_8x8_mode= get_bits1(&s->gb);
        decode_scaling_matrices(h, h->sps_buffers[pps->sps_id], pps, 0, pps->scaling_matrix4, pps->scaling_matrix8);
        pps->chroma_qp_index_offset[1]= get_se_golomb(&s->gb); //second_chroma_qp_index_offset
    } else {
        pps->chroma_qp_index_offset[1]= pps->chroma_qp_index_offset[0];
    }

    build_qp_table(pps, 0, pps->chroma_qp_index_offset[0], h->sps.bit_depth_luma);
    build_qp_table(pps, 1, pps->chroma_qp_index_offset[1], h->sps.bit_depth_luma);
    if(pps->chroma_qp_index_offset[0] != pps->chroma_qp_index_offset[1])
        pps->chroma_qp_diff= 1;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "pps:%u sps:%u %s slice_groups:%d ref:%d/%d %s qp:%d/%d/%d/%d %s %s %s %s\n",
               pps_id, pps->sps_id,
               pps->cabac ? "CABAC" : "CAVLC",
               pps->slice_group_count,
               pps->ref_count[0], pps->ref_count[1],
               pps->weighted_pred ? "weighted" : "",
               pps->init_qp, pps->init_qs, pps->chroma_qp_index_offset[0], pps->chroma_qp_index_offset[1],
               pps->deblocking_filter_parameters_present ? "LPAR" : "",
               pps->constrained_intra_pred ? "CONSTR" : "",
               pps->redundant_pic_cnt_present ? "REDU" : "",
               pps->transform_8x8_mode ? "8x8DCT" : ""
               );
    }

    av_free(h->pps_buffers[pps_id]);
    h->pps_buffers[pps_id]= pps;
    return 0;
fail:
    av_free(pps);
    return -1;
}
static void release_seq_parameter_set_mvc_extension(SPS_MVC_EXT* mvc_ext)
{
    int i = 0,j = 0;
    if(mvc_ext == NULL)
    {
        return ;
    }
    
    if(mvc_ext->view_id != NULL)
    {
        av_free(mvc_ext->view_id);
    }

    if(mvc_ext->ancher_refs != NULL)
    {
        for(i = 0; i < mvc_ext->num_views-1; i++)
        {
            if(mvc_ext->ancher_refs[i].anchor_refs_l0 != NULL)
            {
                av_free(mvc_ext->ancher_refs[i].anchor_refs_l0);
            }

            if(mvc_ext->ancher_refs[i].anchor_refs_l1 != NULL)
            {
                av_free(mvc_ext->ancher_refs[i].anchor_refs_l0);
            }
        }

        av_free(mvc_ext->ancher_refs);
    }

    if(mvc_ext->non_ancher_refs != NULL)
    {
        for(i = 0; i < mvc_ext->num_views-1; i++)
        {
            if(mvc_ext->non_ancher_refs[i].anchor_refs_l0 != NULL)
            {
                av_free(mvc_ext->non_ancher_refs[i].anchor_refs_l0);
            }

            if(mvc_ext->non_ancher_refs[i].anchor_refs_l1 != NULL)
            {
                av_free(mvc_ext->non_ancher_refs[i].anchor_refs_l0);
            }
        }

        av_free(mvc_ext->non_ancher_refs);
    }


    if(mvc_ext->applicable_op != NULL)
    {
        for(i = 0; i < mvc_ext->num_level_value_signalled; i++)
        {
            if(mvc_ext->applicable_op[i].applicable_ops != NULL)
            {
                for(j = 0; j < mvc_ext->applicable_op[i].num_applicable_ops; j++)
                {
                    if(mvc_ext->applicable_op[i].applicable_ops[j].view != NULL)
                    {
                        av_free(mvc_ext->applicable_op[i].applicable_ops[j].view);
                    }
                }

                av_free(mvc_ext->applicable_op[i].applicable_ops);
            }
        }
        av_free(mvc_ext->applicable_op);
    }

    av_free(mvc_ext);
}

static inline int decode_seq_parameter_set_mvc_extension(H264Context *h,SPS_MVC_EXT **mvc_ext)
{
    MpegEncContext * const s = &h->s;
    SPS *sps = &h->sps;
    int i = 0, j = 0, k = 0;

    SPS_MVC_EXT* sps_mvc_ext = av_mallocz(sizeof(SPS_MVC_EXT));
    if(sps_mvc_ext == NULL)
    {
        return -1;
    }
    
    sps_mvc_ext->num_views = get_ue_golomb(&s->gb) + 1; // 0 to 1023
    av_log(h->s.avctx, AV_LOG,"decode_seq_parameter_set_mvc_extension(),num_views = %d",sps_mvc_ext->num_views);
    sps_mvc_ext->view_id = av_mallocz(sizeof(int)*sps_mvc_ext->num_views);
    if(sps_mvc_ext->view_id != NULL)
    {
        for(i = 0; i < sps_mvc_ext->num_views; i++)
        {
            sps_mvc_ext->view_id[i] = get_ue_golomb(&s->gb) + 1; //  0 to 1023
            av_log(h->s.avctx, AV_LOG, "decode_seq_parameter_set_mvc_extension(),view_id[%d]  = %d",i,sps_mvc_ext->view_id[i]);
        }
    }
    else
    {
        goto fail;
    }

    sps_mvc_ext->ancher_refs = av_mallocz(sizeof(SPS_EXT_ANCHER)*(sps_mvc_ext->num_views-1));
    if(sps_mvc_ext->ancher_refs == NULL)
    {
        goto fail;
    }
    
    for(i = 0; i < sps_mvc_ext->num_views-1; i++)
    {
        sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0 = get_ue_golomb_31(&s->gb);//get_ue_golomb(&s->gb);
        av_log(h->s.avctx, AV_LOG, "decode_seq_parameter_set_mvc_extension(),sps_mvc_ext->ancher_refs[%d] = %d",
            i,sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0);
        if((sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0 > 0) && (sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0 <= 15))
        {
            sps_mvc_ext->ancher_refs[i].anchor_refs_l0 = av_mallocz(sizeof(int)*sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0);
            if(sps_mvc_ext->ancher_refs[i].anchor_refs_l0 == NULL)
            {
                av_log(h->s.avctx, AV_LOG_ERROR, "anchor_refs[%d].anchor_refs_l0 = NULL");
                goto fail;
            }
            
            for(j = 0; j< sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0; j++)
            {
                sps_mvc_ext->ancher_refs[i].anchor_refs_l0[j] = get_ue_golomb(&s->gb);
                av_log(h->s.avctx, AV_LOG, "decode_seq_parameter_set_mvc_extension(),sps_mvc_ext->ancher_refs[%d].anchor_refs_l0[%d] = %d",
                    i,j,sps_mvc_ext->ancher_refs[i].anchor_refs_l0[j]);
            }
        }
        else if(sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0 > 15)
        {
            av_log(h->s.avctx, AV_LOG_ERROR, "ancher_refs[%d].number_anchor_ref_l0 = %d count error",
                        i,sps_mvc_ext->ancher_refs[i].number_anchor_ref_l0);
            goto fail;
        }

        sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1 = get_ue_golomb_31(&s->gb);//get_ue_golomb(&s->gb);
        av_log(h->s.avctx, AV_LOG, "decode_seq_parameter_set_mvc_extension(),sps_mvc_ext->number_anchor_ref_l1[%d] = %d",
            i,sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1);
        if((sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1 > 0) && (sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1 <= 15))
        {
            sps_mvc_ext->ancher_refs[i].anchor_refs_l1 = av_mallocz(sizeof(int)*sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1);
            if(sps_mvc_ext->ancher_refs[i].anchor_refs_l1 == NULL)
            {
                av_log(h->s.avctx, AV_LOG_ERROR, "anchor_refs[%d].anchor_refs_l1 = NULL");
                goto fail;
            }
            
            for(j = 0; j< sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1; j++)
            {
                sps_mvc_ext->ancher_refs[i].anchor_refs_l1[j] = get_ue_golomb(&s->gb);
                av_log(h->s.avctx, AV_LOG, "decode_seq_parameter_set_mvc_extension(),sps_mvc_ext->ancher_refs[%d].anchor_refs_l1[%d] = %d",
                    i,j,sps_mvc_ext->ancher_refs[i].anchor_refs_l1[j]);
            }
        }
        else if(sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1 > 15)
        {
            av_log(h->s.avctx, AV_LOG_ERROR, "anchor_refs[%d].number_ancher_ref = %d too larger",i,sps_mvc_ext->ancher_refs[i].number_anchor_ref_l1);
            goto fail;
        }
    }

    sps_mvc_ext->non_ancher_refs = av_mallocz(sizeof(SPS_EXT_ANCHER)*(sps_mvc_ext->num_views-1));
    if(sps_mvc_ext->non_ancher_refs == NULL)
    {
        goto fail;
    }

    for(i = 0; i < sps_mvc_ext->num_views-1; i++)
    {
        sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0 = get_ue_golomb_31(&s->gb);
        if((sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0 > 0) && (sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0 <= 15))
        {
           sps_mvc_ext->non_ancher_refs[i].anchor_refs_l0 = av_mallocz(sizeof(int)*sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0);
            if(sps_mvc_ext->non_ancher_refs[i].anchor_refs_l0 == NULL)
            {
                av_log(h->s.avctx, AV_LOG_ERROR, "non_ancher_refs[%d].anchor_refs_l0 = NULL");
                goto fail;
            }
            
            for(j = 0; j< sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0; j++)
            {
                sps_mvc_ext->non_ancher_refs[i].anchor_refs_l0[j] = get_ue_golomb(&s->gb);
            }
        }
        else if(sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0 > 15)
        {
            av_log(h->s.avctx, AV_LOG_ERROR, "anchor_refs[%d].number_anchor_ref_l0 = %d error",i,sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l0);
            goto fail;
        }

        sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1 = get_ue_golomb_31(&s->gb);
        if((sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1 > 0) && (sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1 <= 15))
        {
            sps_mvc_ext->non_ancher_refs[i].anchor_refs_l1 = av_mallocz(sizeof(int)*sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1);
            if(sps_mvc_ext->non_ancher_refs[i].anchor_refs_l1 == NULL)
            {
                av_log(h->s.avctx, AV_LOG_ERROR, "non_ancher_refs[%d].anchor_refs_l1 = NULL");
                goto fail;
            }
            
            for(j = 0; j< sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1; j++)
            {
                sps_mvc_ext->non_ancher_refs[i].anchor_refs_l1[j] = get_ue_golomb(&s->gb);
            }
        }
        else if(sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1 > 15)
        {
            av_log(h->s.avctx, AV_LOG_ERROR, "non_ancher_refs[%d].number_anchor_ref_l1 = %d error",i,sps_mvc_ext->non_ancher_refs[i].number_anchor_ref_l1);
            goto fail;
        }
        
    }
    
    sps_mvc_ext->num_level_value_signalled = get_ue_golomb(&s->gb) + 1;
    av_log(h->s.avctx, AV_LOG, "num_level_value_signalled = %d",sps_mvc_ext->num_level_value_signalled);
    sps_mvc_ext->applicable_op = av_mallocz(sizeof(SPS_EXT_APPLICABEL_OP)*sps_mvc_ext->num_level_value_signalled);
    if(sps_mvc_ext->applicable_op == NULL)
    {
        av_log(h->s.avctx, AV_LOG_ERROR, "applicable_op = NULL");
        goto fail;
    }
    
    for(i = 0; i < sps_mvc_ext->num_level_value_signalled; i++)
    {
        sps_mvc_ext->applicable_op[i].level_idc = get_bits(&s->gb, 8);
        sps_mvc_ext->applicable_op[i].num_applicable_ops = get_ue_golomb(&s->gb) + 1;

        av_log(h->s.avctx, AV_LOG, "level_idc[%d] = %d,num_applicable_ops[%d] = %d",//AV_LOG
                i,sps_mvc_ext->applicable_op[i].level_idc,i,sps_mvc_ext->applicable_op[i].num_applicable_ops);
        
        sps_mvc_ext->applicable_op[i].applicable_ops = av_mallocz(sizeof(SPS_EXT_APPLICABEL_OPS)*sps_mvc_ext->applicable_op[i].num_applicable_ops);
        if(sps_mvc_ext->applicable_op[i].applicable_ops == NULL)
        {
            av_log(h->s.avctx, AV_LOG_ERROR, "applicable_op = NULL");
            goto fail;
        }
        
        for(j = 0; j < sps_mvc_ext->applicable_op[i].num_applicable_ops ; j++)
        {
             sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_op_temporal_id = get_bits(&s->gb, 3);
             sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_num_target_view = get_ue_golomb(&s->gb) + 1;

             av_log(h->s.avctx, AV_LOG, "applicable_op_temporal_id[%d] = %d,applicable_num_target_view[%d] = %d",
                j,sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_op_temporal_id,
                j,sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_num_target_view);
             
             sps_mvc_ext->applicable_op[i].applicable_ops[j].view = 
                    av_mallocz(sizeof(SPS_EXT_APP_OP_TARGET_VIEW)*sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_num_target_view);

             if(sps_mvc_ext->applicable_op[i].applicable_ops[j].view == NULL)
             {
                av_log(h->s.avctx, AV_LOG_ERROR, "applicable_op = NULL");
                goto fail;
             }
             
             for(k = 0; k < sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_num_target_view; k++)
             {
                 sps_mvc_ext->applicable_op[i].applicable_ops[j].view[k].applicable_op_target_view_id = get_ue_golomb(&s->gb);
                 
                 av_log(h->s.avctx, AV_LOG, "applicable_num_target_view[%d] = %d",
                    k,sps_mvc_ext->applicable_op[i].applicable_ops[j].view[k].applicable_op_target_view_id);
             }

             sps_mvc_ext->applicable_op[i].applicable_ops[j].applicable_op_num_view = get_ue_golomb(&s->gb);
        }
    }

    *mvc_ext = sps_mvc_ext;
    return 0;

fail:
    release_seq_parameter_set_mvc_extension(sps_mvc_ext);
    return -1;    
}

static inline int decode_sps_ext_hrd_parameters(H264Context *h, HRD_PARAMETER *hrd)
{
    MpegEncContext * const s = &h->s;
    int cpb_count, i;
    if((hrd == NULL) || (h == NULL))
    {
        return -1;
    }
    
    cpb_count = get_ue_golomb_31(&s->gb) + 1;
    if(cpb_count > 32U)
    {
        av_log(h->s.avctx, AV_LOG_ERROR, "decode_sps_ext_hrd_parameters():cpb_count %d invalid\n", cpb_count);
        return -1;
    }

    hrd->cpb_count = cpb_count;
    hrd->bit_rate_scale = get_bits(&s->gb, 4); /* bit_rate_scale */
    hrd->cpb_size_scale = get_bits(&s->gb, 4); /* cpb_size_scale */
    
    for(i=0; i<cpb_count; i++)
    {
        get_ue_golomb_long(&s->gb); /* bit_rate_value_minus1 */
        get_ue_golomb_long(&s->gb); /* cpb_size_value_minus1 */
        get_bits1(&s->gb);     /* cbr_flag */
    }
    
    hrd->initial_cpb_removal_delay_length = get_bits(&s->gb, 5) + 1;
    hrd->cpb_removal_delay_length = get_bits(&s->gb, 5) + 1;
    hrd->dpb_output_delay_length = get_bits(&s->gb, 5) + 1;
    hrd->time_offset_length = get_bits(&s->gb, 5);
    return 0;
}


static void release_mvc_vui_parameter_extension(SPS_VUI_PARAMETER_EXT* vui_para_ext)
{
    int i = 0;
    if(vui_para_ext != NULL)
    {
        for(i = 0; i < vui_para_ext->vui_mvc_num_ops; i++)
        {
            if(vui_para_ext->vui_mvc_ops[i].view != NULL)
            {
                av_free(vui_para_ext->vui_mvc_ops[i].view);
            }

            if(vui_para_ext->vui_mvc_ops[i].nal_hrd == NULL)
            {
                av_free(vui_para_ext->vui_mvc_ops[i].nal_hrd);
            }

            if(vui_para_ext->vui_mvc_ops[i].vcl_hrd == NULL)
            {
                av_free(vui_para_ext->vui_mvc_ops[i].vcl_hrd);
            }
        }
        
        if(vui_para_ext->vui_mvc_ops != NULL)
        {
            av_free(vui_para_ext->vui_mvc_ops);
        }

        av_free(vui_para_ext);
    }
}


static inline int decode_mvc_vui_parameter_extension(H264Context *h,SPS_VUI_PARAMETER_EXT** vui_para)
{
    MpegEncContext * const s = &h->s;
    SPS *sps = &h->sps;
    int i = 0, j = 0, k = 0;
    SPS_VUI_MVC_OPS* mvc_ops = NULL;
    

    SPS_VUI_PARAMETER_EXT* vui_para_ext = av_mallocz(sizeof(SPS_VUI_PARAMETER_EXT));
    if(vui_para_ext == NULL)
    {
        goto fail;
    }
    
    vui_para_ext->vui_mvc_num_ops = get_ue_golomb(&s->gb) + 1;
    av_log(h->s.avctx, AV_LOG, "decode_mvc_vui_parameter_extension():vui_mvc_num_ops = %d",vui_para_ext->vui_mvc_num_ops);
    
    vui_para_ext->vui_mvc_ops = av_mallocz(sizeof(SPS_VUI_MVC_OPS)*vui_para_ext->vui_mvc_num_ops);
    if(vui_para_ext->vui_mvc_ops == NULL)
    {
        av_log(h->s.avctx, AV_LOG_ERROR, "vui_para_ext->vui_mvc_ops = NULL");
        goto fail;
    }
    
    for(i = 0; i < vui_para_ext->vui_mvc_num_ops; i++)
    {
        vui_para_ext->vui_mvc_ops[i].vui_mvc_temporal_id = get_bits(&s->gb,3);
        vui_para_ext->vui_mvc_ops[i].vui_mvc_num_target_output_views = get_ue_golomb(&s->gb) + 1;

        av_log(h->s.avctx, AV_LOG, 
            "decode_mvc_vui_parameter_extension():vui_mvc_temporal_id = %d,vui_mvc_num_target_output_views = %d",
            vui_para_ext->vui_mvc_ops[i].vui_mvc_temporal_id,
            vui_para_ext->vui_mvc_ops[i].vui_mvc_num_target_output_views);
        
        vui_para_ext->vui_mvc_ops[i].view = 
                    av_mallocz(sizeof(SPS_VUI_MVC_TARGET_OUTPUT_VIEW)*vui_para_ext->vui_mvc_ops[i].vui_mvc_num_target_output_views);
        if(vui_para_ext->vui_mvc_ops[i].view == NULL)
        {
            av_log(h->s.avctx, AV_LOG_ERROR, "vui_para_ext->vui_mvc_ops[%d].view = NULL",i);
            goto fail;
        }

        for(j = 0; j < vui_para_ext->vui_mvc_ops[i].vui_mvc_num_target_output_views; j++)
        {
            vui_para_ext->vui_mvc_ops[i].view[j].vui_mvc_view_id = get_ue_golomb(&s->gb);
            av_log(h->s.avctx, AV_LOG, 
                "decode_mvc_vui_parameter_extension():vui_mvc_view_id[%d][%d] = %d",
                i,j,
                vui_para_ext->vui_mvc_ops[i].view[j].vui_mvc_view_id);
        }

        vui_para_ext->vui_mvc_ops[i].vui_mvc_timing_info_present_flag = get_bits(&s->gb,1);
        if(vui_para_ext->vui_mvc_ops[i].vui_mvc_timing_info_present_flag)
        {
            vui_para_ext->vui_mvc_ops[i].vui_mvc_num_units_in_ticks = get_bits(&s->gb,32);
            vui_para_ext->vui_mvc_ops[i].vui_mvc_time_scale = get_bits(&s->gb,32);
            vui_para_ext->vui_mvc_ops[i].vui_mvc_fixed_frame_rate_flag = get_bits(&s->gb,1);
        }

        vui_para_ext->vui_mvc_ops[i].vui_mvc_nal_hrd_parameters_present_flag = get_bits(&s->gb,1);
        if(vui_para_ext->vui_mvc_ops[i].vui_mvc_nal_hrd_parameters_present_flag)
        {
            vui_para_ext->vui_mvc_ops[i].nal_hrd = av_mallocz(sizeof(HRD_PARAMETER));
            if(vui_para_ext->vui_mvc_ops[i].nal_hrd == NULL)
            {
                av_log(h->s.avctx, AV_LOG_ERROR, "vui_para_ext->vui_mvc_ops[%d].nal_hrd = NULL",i);
                goto fail;
            }
            decode_sps_ext_hrd_parameters(h,vui_para_ext->vui_mvc_ops[i].nal_hrd);
        }

        vui_para_ext->vui_mvc_ops[i].vui_mvc_vcl_hrd_parameters_present_flag = get_bits(&s->gb,1);
        if(vui_para_ext->vui_mvc_ops[i].vui_mvc_vcl_hrd_parameters_present_flag)
        {
            vui_para_ext->vui_mvc_ops[i].vcl_hrd = av_mallocz(sizeof(HRD_PARAMETER));
            if(vui_para_ext->vui_mvc_ops[i].vcl_hrd == NULL)
            {
                av_log(h->s.avctx, AV_LOG_ERROR, "vui_para_ext->vui_mvc_ops[%d].vcl_hrd = NULL",i);
                goto fail;
            }
            decode_sps_ext_hrd_parameters(h,vui_para_ext->vui_mvc_ops[i].vcl_hrd);
        }

        if(vui_para_ext->vui_mvc_ops[i].vui_mvc_nal_hrd_parameters_present_flag ||
            vui_para_ext->vui_mvc_ops[i].vui_mvc_vcl_hrd_parameters_present_flag)
        {
            vui_para_ext->vui_mvc_ops[i].vui_mvc_low_delay_hrd_present_flag = get_bits(&s->gb,1);
        }
        
        vui_para_ext->vui_mvc_ops[i].vui_mvc_pic_struct_present_flag = get_bits(&s->gb,1);
    }

    *vui_para = vui_para_ext;
    return 0;
fail:
    release_mvc_vui_parameter_extension(vui_para_ext);
    return -1;
}

void release_subset_seq_parameter_set(SPS_EXT* ext)
{
    if(ext != NULL)
    {
        if(ext->sps_mvc_ext != NULL)
        {
            release_seq_parameter_set_mvc_extension(ext->sps_mvc_ext);
        }

        if(ext->vui_para_ext != NULL)
        {
            release_mvc_vui_parameter_extension(ext->vui_para_ext);
        }

        av_free(ext);
    }
}


int ff_h264_decode_subset_seq_parameter_set(H264Context *h)
{
    MpegEncContext * const s = &h->s;
    SPS *sps = &h->sps;
    SPS_EXT* sps_ext = NULL;
    
    // firset parse sps,if parse success, then parse sps extesntion
    if(ff_h264_decode_seq_parameter_set(h) == 0)
    {
   //     if(sps != NULL)
        {
            sps_ext = av_mallocz(sizeof(SPS_EXT));
            if(sps_ext == NULL)
            {
                av_log(NULL, AV_LOG_ERROR, "ff_h264_decode_subset_seq_parameter_set():sps_ext == NULL");
                goto fail;
            }
            av_log(h->s.avctx, AV_LOG, "ff_h264_decode_subset_seq_parameter_set(),sps->profile_idc = %d",sps->profile_idc);// AV_LOG
            if((sps->profile_idc == 83) || (sps->profile_idc == 86))
            {
                av_log(NULL, AV_LOG_ERROR, "ff_h264_decode_subset_seq_parameter_set():profile_idc = %d not support",sps->profile_idc);
                goto fail;
            }
            else if((sps->profile_idc == 118) || (sps->profile_idc == 128))
            {
                int bit_equal_to_one =  get_bits(&s->gb, 1); 
                decode_seq_parameter_set_mvc_extension(h,&(sps_ext->sps_mvc_ext));
                
                sps_ext->mvc_vui_parameter_present_flag = get_bits(&s->gb, 1);
                if(sps_ext->mvc_vui_parameter_present_flag == 1)
                {
                    decode_mvc_vui_parameter_extension(h,&(sps_ext->vui_para_ext));
                }
            }
            else if(sps->profile_idc == 138)
            {
                av_log(NULL, AV_LOG_ERROR, "ff_h264_decode_subset_seq_parameter_set():profile_idc = %d not support",sps->profile_idc);
                goto fail;
            }
            
            sps_ext->additional_extension2_flag = get_bits(&s->gb, 1);
            if(sps_ext->additional_extension2_flag == 1)
            {
             //   int bits_left = bit_length - get_bits_count(&s->gb);
             //   while(bit_length - get_bits_count(&s->gb))//(bits_left > 0 && more_rbsp_data_in_pps(h, pps))
                while(get_bits_left(&s->gb))
                {
                    int additinal_extension2_data_flag = get_bits(&s->gb, 1);
                }
            }

            sps->sps_ext = sps_ext; 
            return 0;
        }
    }
    else
    {
        av_log(h->s.avctx, AV_LOG_ERROR, "ff_h264_decode_subset_seq_parameter_set(): ff_h264_decode_seq_parameter_set fail");
    }


fail:
    if(sps_ext != NULL)
    {
        release_subset_seq_parameter_set(sps_ext);
    }
    return -1;

}
