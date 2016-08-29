#include "rs_fec.h"

#include "internal.h"
#include "avformat.h"

#include "libavutil/intreadwrite.h"
#include "rtp.h"

static void print_table(TableContext256 *ctx){
     if(ctx == NULL) return;
     for (int i = 0; i <= (int)Q; ++i) {
        av_log(NULL,AV_LOG_ERROR," m_log[%d] = %d,m_exp[%d] = %d",i,ctx->m_log[i],i,ctx->m_exp[i]);
    }
}

void table_context256_init(TableContext256 *ctx){
    BYTE x = 0x1;
    XWORD y = 0x1;
    ctx->m_exp[Q] = 0;
//    av_log(NULL,AV_LOG_ERROR,"%s", __FUNCTION__);
    for (int i = 0; i < (int)Q; ++i) {
        ctx->m_exp[i] = x;
        y <<= 1;
        if (y & 0x100)
            y ^= MODULUS;
        y %= 0x100;
        x = y;
    }
    
    for (int i = 0; i <= (int)Q; ++i) {
        ctx->m_log[ctx->m_exp[i]] = i;
    }

//    print_table(ctx);
}

void matrix_invGF256(RSFecContext *ctx,PBYTE* matrix, const int n) {
    int i, j, k, l, ll;
    int irow = 0, icol = 0;
    BYTE dum, big, temp;
    XWORD pivinv;
    
    int indxc[BOUND], indxr[BOUND], ipiv[BOUND];
    
    for (j = 0; j < n; ++j) {
        indxc[j] = 0;
        indxr[j] = 0;
        ipiv[j] = 0;
    }
    for (i = 0; i < n; ++i) {
        
        big = 0;
        for (j = 0; j < n; ++j)
            if (ipiv[j] != 1)
                for (k = 0; k < n; ++k) {
                    if (ipiv[k] == 0) {
                        if (matrix[j][k] >= big) {
                            big = matrix[j][k];
                            irow = j;
                            icol = k;    
                        }
                   
                    }
                    else if (ipiv[k] > 1){
                        av_log(NULL,AV_LOG_ERROR,"[RTPFEC]%32s --> [BAD Parameter]ipiv[k] > 1", __FUNCTION__);
                    };  
                }
        ++(ipiv[icol]);
        
        
        if (irow != icol)
            for (l = 0; l < n; ++l) SWAP(matrix[irow][l],matrix[icol][l])  
        
        indxr[i] = irow;
        indxc[i] = icol;
        if (matrix[icol][icol] == 0) 
            av_log(NULL,AV_LOG_ERROR,"[RTPFEC]%32s --> [BAD Parameter]matrix[icol][icol] == 0", __FUNCTION__);
        pivinv = Q - ctx->table.m_log[matrix[icol][icol]];
        matrix[icol][icol] = 0x1;
        for (l = 0; l < n; ++l) 
			if ( matrix[icol][l] )
				matrix[icol][l] = ctx->table.m_exp[(ctx->table.m_log[matrix[icol][l]]+pivinv)%Q]; 
        for (ll = 0; ll < n; ++ll)
            if (ll != icol) {
                dum = matrix[ll][icol];
                matrix[ll][icol] = 0;
                for (l = 0; l < n; ++l) 
                    if (matrix[icol][l] && dum)
                        matrix[ll][l] ^= ctx->table.m_exp[(ctx->table.m_log[matrix[icol][l]]+ctx->table.m_log[dum]) % Q];    
            }
	}
    for (l = n-1; l >= 0; --l) {
		if (indxr[l] != indxc[l]) 
            for (k = 0; k < n; ++k)
                SWAP(matrix[k][indxr[l]], matrix[k][indxc[l]])
    }  
}; 


void matrix_mulGF256(RSFecContext *ctx,PBYTE *a, PBYTE *b, PBYTE *c, int left, int mid, int right) {
    int i, j, k;
    for (i = 0; i < left; ++i)
        for (j = 0; j < right; ++j)
            for (k = 0; k < mid; ++k) {
                if (a[i][k] && b[k][j] ) { 
                    c[i][j] ^= ctx->table.m_exp[(ctx->table.m_log[a[i][k]]+ctx->table.m_log[b[k][j]]) % Q];
                }
            }
};

int fec_encode(RSFecContext *ctx, PBYTE *data, PBYTE *fec_data) {
	
	int K = ctx->data_pkt_num;
	int M = ctx->fec_pkt_num;
	int S = ctx->data_len;

	matrix_mulGF256(ctx, ctx->en_GM,data,fec_data,M,K,S);

	return 0;
};

int fec_decode(RSFecContext *ctx, PBYTE *data, PBYTE *fec_data, int lost_map[]) {
	
	int K = ctx->data_pkt_num;
	const int M = ctx->fec_pkt_num;
	int N = K + M;
	int S = ctx->data_len;

	int recv_count = 0;
	int tmp_count = 0;
//       av_log(NULL, AV_LOG_ERROR, "%s,K = %d,M = %d,N = %d,S = %d",__FUNCTION__,K,M,N,S);
	#if 0
	for(int i = 0; i < K; i++){
	    av_log(NULL, AV_LOG_ERROR, "%s,data[%d] = %p",__FUNCTION__,i,data[i]);
	}

       for(int i = 0; i <M; i++){
	    av_log(NULL, AV_LOG_ERROR, "%s,fec_data[%d] = %p",__FUNCTION__,i,fec_data[i]);
	}

       for(int i = 0; i < N; i++){
	    av_log(NULL, AV_LOG_ERROR, "%s,lost_map[%d] = %p",__FUNCTION__,i,lost_map[i]);
	}
	#endif
	
	int *lost_pkt_id = (int*)av_malloc(M*sizeof(int));    //new int[M];
	for(int i = 0; i < M; ++i)
		lost_pkt_id[i] = 0;
	int lost_pkt_cnt = 0;

	PBYTE *de_subGM = (PBYTE *)av_malloc(sizeof(PBYTE)*K);  //new PBYTE[K];

	for (int i = 0; i < K; ++i) {
		de_subGM[i] = (BYTE*)av_malloc(sizeof(BYTE)*K); //new BYTE[K];
		for (int j = 0; j < K; ++j)
			de_subGM[i][j] = 0;
	}
       
	for (int i = 0; i < K; ++i) { 
		if (lost_map[i] == 1) {
			de_subGM[recv_count][i] = 1;
			++recv_count;
		}
		else
			lost_pkt_id[lost_pkt_cnt++] = i; // recoder index
	}
       
	for (int i = K; i < N; ++i) { 
		if (lost_map[i] == 1) {
			if (recv_count < K) {  
				for (int j = 0; j < K; ++j)
					de_subGM[recv_count][j] = ctx->en_GM[i-K][j];
				++recv_count;
			}
			else 
				break;
		}
	}
       
	matrix_invGF256(ctx, de_subGM,K);
       
	PBYTE *recv_data = (PBYTE*)av_malloc(sizeof(PBYTE)*K); //new PBYTE[K];

	for (int i = 0; i < K; ++i) 
		recv_data[i] = (BYTE*)av_malloc(sizeof(BYTE)*S); //new BYTE[S];

	for (int i = 0; i < N; ++i) {
		if (lost_map[i]) {
			if(i <  K) {
				memcpy(recv_data[tmp_count],data[i],S);
			}
			else{
				memcpy(recv_data[tmp_count],fec_data[i-K],S);
			}
			++tmp_count;
		}
		if (tmp_count == K)
			break;
	}

 //      print_table(&(ctx->table));	
	for(int i = 0; i < lost_pkt_cnt; ++i) {
		int cur_lost_pkt = lost_pkt_id[i];
		memset(data[cur_lost_pkt],0,S);
//	    av_log(NULL, AV_LOG_ERROR, "%s,data[%d]= %p",__FUNCTION__,cur_lost_pkt,data[cur_lost_pkt]);
		for(int r = 0; r < S; ++r) {
			for(int l = 0; l < K; ++l) {
				if (de_subGM[cur_lost_pkt][l] && recv_data[l][r])
					data[cur_lost_pkt][r] ^= ctx->table.m_exp[(ctx->table.m_log[de_subGM[cur_lost_pkt][l]]+ctx->table.m_log[recv_data[l][r]]) % Q];
			}
		}
	}
       
	for (int i = 0; i < K; ++i) {
		av_free(de_subGM[i]);
		av_free(recv_data[i]);
	}
       
	av_free(de_subGM);
	av_free(recv_data);
	av_free(lost_pkt_id);

	return 0;
};

void fec_init(RSFecContext *ctx, int data_pkt_num, int fec_pkt_num, int data_len) {
       table_context256_init(&(ctx->table));
	ctx->data_pkt_num = data_pkt_num;
	ctx->fec_pkt_num = fec_pkt_num;
	ctx->data_len = data_len;

	const int K = ctx->data_pkt_num;
	const int M = ctx->fec_pkt_num;

	ctx->en_left = av_malloc(sizeof(PBYTE)*M); //new PBYTE[M];
	ctx->en_right = av_malloc(sizeof(PBYTE)*K); //new PBYTE[K];
	ctx->en_GM = av_malloc(sizeof(PBYTE)*M); //new PBYTE[M];
	
	for (int i = 0, _i = K; i < M; ++i, ++_i) {
		ctx->en_left[i] = av_malloc(sizeof(BYTE)*K); //new BYTE[K];
		ctx->en_GM[i] = av_malloc(sizeof(BYTE)*K); //new BYTE[K];
		
		for (int j = 0; j < K; ++j) {
			ctx->en_left[i][j] = ctx->table.m_exp[(_i*j)%Q]; 
			ctx->en_GM[i][j] = 0;
		}
	}

	for (int i = 0; i < K; ++i) {
		ctx->en_right[i] = av_malloc(sizeof(BYTE)*K); //new BYTE[K];
		for (int j = 0; j < K; ++j)
			ctx->en_right[i][j] = ctx->table.m_exp[(i*j)%Q];
	}	

	matrix_invGF256(ctx, ctx->en_right, K);
	
	matrix_mulGF256(ctx, ctx->en_left,ctx->en_right,ctx->en_GM,M,K,K);
};

void fec_cleanup(RSFecContext *ctx) {
	int K = ctx->data_pkt_num;
	int M = ctx->fec_pkt_num;

	for (int i = 0; i < K; ++i) 
		 av_free(ctx->en_right[i]);

	 for (int i = 0; i < M; ++i) {
		 av_free(ctx->en_left[i]);
		 av_free(ctx->en_GM[i]);
	 }

	 av_free(ctx->en_left);
	 av_free(ctx->en_right);
	 av_free(ctx->en_GM);
};

