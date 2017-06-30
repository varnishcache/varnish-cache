#ifndef RSM_H_INCLUDED
#define RSM_H_INCLUDED

#include <sys/mman.h>
#include <sys/stat.h>
#include <limits.h>

#include <vin.h>

#define VSM_DIRNAME		"_replay"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

typedef struct rsm_gen_entry {
	int uptime;
	int nthreads;
	int nreqs;
	char appname[PATH_MAX];
} rsm_gen_entry;

typedef struct rsm_stt_entry {
	int status;
	int neq_status;
} rsm_stt_entry;

typedef struct rsm_err_entry {
	int count;
} rsm_err_entry;

typedef struct rsm_thr_entry {
	int id;
	int reqs;
	int resp;
	int fd;
	int lines;
} rsm_thr_entry;

#define MAX_STT_CNT 1024
#define MAX_ERR_CNT 8
#define MAX_THR_CNT 8192

typedef struct replay_shm {
	rsm_gen_entry rsm_gen;
	rsm_stt_entry rsm_stt[MAX_STT_CNT];
	rsm_err_entry rsm_err[MAX_ERR_CNT];
	rsm_thr_entry rsm_thr[MAX_THR_CNT];
} replay_shm;


void rsm_atexit(void);
void rsm_init(const char* app);
int rsm_open();
void rsm_close();
int rsm_reopen();

extern struct replay_shm* RSM_head;

#endif