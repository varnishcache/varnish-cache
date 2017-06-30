#include "stat_cnt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "rsm.h"
#include "vas.h"
#include "vtim.h"


extern struct replay_shm* RSM_head;


static pthread_mutex_t cnt_lock;
static double st_time;

void inc_stt_rsm(int ind, bool neq)
{
	if (ind >= MAX_STT_CNT) return; // + error
	double now = VTIM_real();
	pthread_mutex_lock(&cnt_lock);
	if (neq) RSM_head->rsm_stt[ind].neq_status++;
	RSM_head->rsm_stt[ind].status++;
	RSM_head->rsm_gen.uptime = (int) (now - st_time);
	pthread_mutex_unlock(&cnt_lock);
}

// this one instead inc_stt_rsm() & inc_thr_resp_rsm()
void inc_stt_resp_rsm(int ind, bool neq, int tind)
{
	if (ind >= MAX_STT_CNT || tind >= MAX_THR_CNT) return; // + error
	double now = VTIM_real();
	pthread_mutex_lock(&cnt_lock);
	if (neq) RSM_head->rsm_stt[ind].neq_status++;
	RSM_head->rsm_stt[ind].status++;
	RSM_head->rsm_thr[tind].resp++;
	RSM_head->rsm_gen.uptime = (int) (now - st_time);
	pthread_mutex_unlock(&cnt_lock);
}

void inc_err_rsm(int ind)
{
	if (ind >= MAX_ERR_CNT) return; // + error
	double now = VTIM_real();
	pthread_mutex_lock(&cnt_lock);
	RSM_head->rsm_err[ind].count++;
	RSM_head->rsm_gen.uptime = (int) (now - st_time);
	pthread_mutex_unlock(&cnt_lock);
}

void inc_thr_req_rsm(int ind)
{
	if (ind >= MAX_THR_CNT) return; // + error
	double now = VTIM_real();
	pthread_mutex_lock(&cnt_lock);
	RSM_head->rsm_gen.nreqs++;
	RSM_head->rsm_thr[ind].reqs++;
	RSM_head->rsm_gen.uptime = (int) (now - st_time);
	pthread_mutex_unlock(&cnt_lock);
}

void inc_thr_resp_rsm(int ind)
{
	if (ind >= MAX_THR_CNT) return; // + error
	double now = VTIM_real();
	pthread_mutex_lock(&cnt_lock);
	RSM_head->rsm_thr[ind].resp++;
	RSM_head->rsm_gen.uptime = (int) (now - st_time);
	pthread_mutex_unlock(&cnt_lock);
}

void inc_thr_line_rsm(int ind)
{
	if (ind >= MAX_THR_CNT) return; // + error
	pthread_mutex_lock(&cnt_lock);
	RSM_head->rsm_thr[ind].lines++;
	pthread_mutex_unlock(&cnt_lock);
}

void set_nthr_rsm(int nthr)
{
	int i;
	pthread_mutex_lock(&cnt_lock);
	for (i = 0; i < nthr; i++) RSM_head->rsm_thr[i].id = i;
	RSM_head->rsm_gen.nthreads = nthr;
	pthread_mutex_unlock(&cnt_lock);
}

void set_thr_fd_rsm(int ind, int fd)
{
	if (ind >= MAX_THR_CNT) return; // + error
	pthread_mutex_lock(&cnt_lock);
	RSM_head->rsm_thr[ind].fd = fd;
//RSM_head->rsm_thr[ind].lines++;
	pthread_mutex_unlock(&cnt_lock);
}

/*--------------------------------------------------------------------
 * Counter stat part
 */

#define cnt_arr_size (MAX_EXT_ARR_IND + 1)
static int cnt_arr[cnt_arr_size];

static char* cnt_arr_names[] = {"STT", "STT_NE", "STT_2N", "S_200", "SN_200", "LEN", "LEN_NE", "ENC", "ENC_NE", "CHK", "CHK_NE", "CEN", "CEN_NE", "TOT", "MTD_NS", "IN_ERR", "TOT_ERR"};
static char* cnt_arr_descs[] = {
	"Statuses are equal (expect == orig)", "Statuses aren't equal", "Statuses aren't equal (200)",
	"Status 200", "Statuses not 200", 
	"Lengths are equal", "Lengths aren't equal",
	"Encoded resp", "Encoded, lengths aren't equal",
	"Chunked resp", "Chunked, lengths aren't equal",
	"Encoded & chunked resp", "Encoded & chunked resp, lengths aren't equal",
	"Total",
	"Not Supported Method", "Internal Error"
};


void cnt_init()
{
	AZ(pthread_mutex_init(&cnt_lock, NULL));
	st_time = VTIM_real();

	memset(&cnt_arr, 0, sizeof(cnt_arr));

	if (! RSM_head) {
		fprintf(stderr, "RSM isn't initialized, exited ..\n");
		exit (1);
	}
}

void inc_cnt(int ind)
{
	if (ind >= MAX_EXT_ARR_IND) return;

	pthread_mutex_lock(&cnt_lock);
	cnt_arr[ind]++;
	pthread_mutex_unlock(&cnt_lock);
}

void prn_cnt(bool print_desc)
{
	int tot = 0;

	if (print_desc) {
		printf("\n\t Results:\n");
		for (int ind = 0; ind <= LAST_ARR_IND; ind++)
			printf(" %7.7s", cnt_arr_names[ind]);
		printf("\n");
	}

	for (int ind = 0; ind <= LAST_ARR_IND; ind++) {
		if (ind != LAST_ARR_IND) {
			printf(" %7d", cnt_arr[ind]);
			if (ind >= FIRST_CNT_IND && ind <= LAST_CNT_IND)
				tot += cnt_arr[ind];
		} else
			printf(" %7d\n", tot);
	}

	tot = 0;
	if (print_desc) {
		for (int ind = LAST_ARR_IND + 1; ind <= LAST_EXT_ARR_IND; ind++)
			printf(" %7.7s", cnt_arr_names[ind]);
		printf("\n");
	}

	for (int ind = LAST_ARR_IND + 1; ind <= LAST_EXT_ARR_IND; ind++) {
		if (ind != LAST_EXT_ARR_IND) {
			printf(" %7d", cnt_arr[ind]);
			if (ind < LAST_EXT_ARR_IND)
				tot += cnt_arr[ind];
		} else
			printf(" %7d\n", tot);
	}

	if (print_desc) {
		printf("\n\n --------------------------------------------------------\n");
		for (int ind = 0; ind < LAST_EXT_ARR_IND; ind++)
			printf("\t %-7.7s\t%s\n", cnt_arr_names[ind], cnt_arr_descs[ind]);
	}
	printf("\n");
	fflush(stdout);
}
