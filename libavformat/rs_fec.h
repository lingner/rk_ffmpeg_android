/*
 * rs_fec definitions
 */
#ifndef AVFORMAT_RS_FEC_H
#define AVFORMAT_RS_FEC_H

#include "libavformat/version.h"

#include "libavutil/log.h"

#define SWAP(a,b) {temp=(a);(a)=(b);(b)=temp;}

typedef unsigned char BYTE;              //GF256 symbol
typedef unsigned int XWORD;              //index of alpha
typedef BYTE* PBYTE;                     //pointer to BYTE
typedef PBYTE* PPBYTE;

//maximum matrix size
#define   BOUND 0x100
//00011101: x^8+x^4+x^3+x^2+1=0
#define MODULUS 0x1d


#define Q (0xff)
#define ALPHA (0x02)

struct udpdata {
    int beginseq;
    int endseq;
    int pktnum;
    int index;
    int length;
    BYTE data[1600];
};

typedef struct _table_context_256{
    BYTE  m_exp[Q+1]; //QQ+1
    XWORD m_log[Q+1]; //QQ+1
} TableContext256;

typedef struct _rs_fec_context{
    int data_pkt_num;
    int fec_pkt_num;
    int data_len;
    int *lost_map;
    TableContext256 table;
    PBYTE *en_left;
    PBYTE *en_right;
    PBYTE *en_GM;
} RSFecContext;

void table_context256_init(TableContext256 *ctx);
void matrix_invGF256(RSFecContext *ctx,PBYTE* matrix, const int n);
void matrix_mulGF256(RSFecContext *ctx,PBYTE *a, PBYTE *b, PBYTE *c, int left, int mid, int right);
void fec_init(RSFecContext *ctx,int data_pkt_num, int fec_pkt_num, int data_len);
void fec_cleanup(RSFecContext *ctx);
int fec_encode(RSFecContext *ctx,PBYTE *data, PBYTE *fec_data);
int fec_decode(RSFecContext *ctx,PBYTE *data, PBYTE *fec_data, int lost_map[]);

#endif

