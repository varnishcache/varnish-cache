#ifndef STAT_CNT_H_INCLUDED
#define STAT_CNT_H_INCLUDED

#include <stdbool.h>

typedef enum {
	STT_IND = 0,
	STT_NEQ_IND,
	STT_200_NEQ_IND,
	STT_200_IND,
	STT_NEQ_200_IND,
	LEN_IND,
	FIRST_CNT_IND = LEN_IND,
	LEN_NEQ_IND,
	ENC_IND, 
	ENC_NEQ_IND, 
	CHK_IND,
	CHK_NEQ_IND,
	CHK_ENC_IND,
	CHK_ENC_NEQ_IND,
	MAX_ARR_IND = CHK_ENC_NEQ_IND,
	LAST_CNT_IND = CHK_ENC_NEQ_IND,
	TOT_IND,
	LAST_ARR_IND = TOT_IND,
	MTD_NS_IND,
	INR_ERR_IND,
	MAX_EXT_ARR_IND = INR_ERR_IND,
	TOT_EXT_IND,
	LAST_EXT_ARR_IND = TOT_EXT_IND
} cnt_ind_e;

void inc_stt_rsm(int ind, bool neq);
void inc_stt_resp_rsm(int ind, bool neq, int tind);
void inc_err_rsm(int ind);
void inc_thr_req_rsm(int ind);
void inc_thr_resp_rsm(int ind);
void inc_thr_line_rsm(int ind);
void set_nthr_rsm(int nthr);
void set_thr_fd_rsm(int ind, int fd);

void cnt_init();
void inc_cnt(int ind);
void prn_cnt(bool print_desc);

typedef enum {
	RSM_ENC_IND = 0,
	RSM_CHK_IND,
	RSM_CHK_ENC_IND,
	RSM_MTD_NS_IND,
	RSM_ERR_IND,
	MAX_ERR_IND,
} rsm_err_ind_e;

static char* const rsm_err_labels[] = {"ENC", "CHK", "CHENC", "MTDNS", "ERROR"};

#endif