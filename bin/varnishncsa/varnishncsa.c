/*
 * $Id:$
 *
 * Program that will get data from the shared memory log. When it has the data
 * it will order the data based on the sessionid. When the data is ordered
 * and session is finished it will write the data into disk. Logging will be
 * in NCSA extended/combined access log format.
 *
 *	"%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-agent}i\""
 * 
 * TODO:	- Log in any format one wants
 *		- Maybe rotate/compress log
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sbuf.h>
#include <vis.h>

#include "shmlog.h"
#include "varnishapi.h"


/* Ordering-----------------------------------------------------------*/


/* We make a array of pointers to sbuf's. Sbuf is a string buffer.
* The buffer can be made/extended/cleared etc. through a API.
* The array is 65536 long because we will use sessionid as key.
*
*/

static struct sbuf	*ob[65536];


/*
* Clean order is called once in a while. It clears all the sessions that 
* where never finished (SLT_SessionClose). Because the data is not complete
* we disregard the data.
*/

static void
clean_order(void)
{
	unsigned u;

	for (u = 0; u < 65536; u++) {
		if (ob[u] == NULL)
			continue;
		sbuf_finish(ob[u]);
		
		/* XXX delete this code? Probably, since we write data to disk/screen
		* as soon as we have all the data we need anyway. If we are here
		* we don't have all the data, hence we don't bother to write out. 
		*
		*
		* if (sbuf_len(ob[u]))
		*	printf("%s\n", sbuf_data(ob[u]));
		*/
			
		sbuf_clear(ob[u]);
	}
}

static void 
extended_log_format(unsigned char *p, char *w_opt)
{
	unsigned u, v;
	int i,j;
	char *ans;
	char soek[1];
	strcpy(soek," ");

	if (w_opt != NULL){
		// printf(" Has w_opt\n");
	} else {
		// printf(" Does not have w_opt\n");
	}

	u = (p[2] << 8) | p[3];
	if (ob[u] == NULL) {
		ob[u] = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
		assert(ob[u] != NULL);
	}
	v = 0;
	switch (p[0]) {

	case SLT_SessionOpen:

		//ans = strchr(&p[4], (int)soek);
		//j = strlen(ans);
		//printf("%d\n",j);
		break;

	case SLT_RxHeader:
	
		if (p[1] >= 11 && !strncasecmp((void *)&p[4], "user-agent:",11)){
			//printf(" User-Agent: %s\n", p[4]);
			//sbuf_printf(ob[u], "%s\n", &p[4]);
			sbuf_bcat(ob[u], p + 4, p[1]);
			sbuf_cat(ob[u], "\n");
			sbuf_finish(ob[u]);
			printf("%s", sbuf_data(ob[u]));
			sbuf_clear(ob[u]);
		}
		break;
	
	case SLT_SessionClose:
		break;
	default:
		v = 1;
		break;
	}
	if (v) {
		
		/* XXX Need to write some code to make the logline 
		sbuf_printf(ob[u], "%02x %3d %4d %-12s",
		    p[0], p[1], u, VSL_tags[p[0]]);
		if (p[1] > 0) {
			sbuf_cat(ob[u], " <");
			sbuf_bcat(ob[u], p + 4, p[1]);
			sbuf_cat(ob[u], ">");
		}
		sbuf_cat(ob[u], "\n");
		*/
	}
	
	/* XXX Do I need this? When is u == 0? I can't seem to see
	* it used before this place.
	if (u == 0) {
		sbuf_finish(ob[u]);
		printf("%s", sbuf_data(ob[u]));
		sbuf_clear(ob[u]);
		return;
	}
	*/
	
}

/*--------------------------------------------------------------------*/

static void
Usage(void)
{
	fprintf(stderr, "Usage: varnishlogfile [-w file] [-r file]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int i, c;
	unsigned u, v;
	unsigned char *p;
	//int o_flag = 0;
	//int l_flag = 0;
	char *w_opt = NULL;
	FILE *wfile = NULL;
	// int h_opt = 0;
	struct VSL_data *vd;

	vd = VSL_New();
	
	while ((c = getopt(argc, argv, VSL_ARGS "w:")) != -1) {
		i = VSL_Arg(vd, c, optarg);
		if (i < 0)
			exit (1);
		if (i > 0)
			continue;
		switch (c) {
		case 'w':
			w_opt = optarg;
			break;
		default:
			Usage();
		}
	}

	if (VSL_OpenLog(vd))
		exit (1);

	if (w_opt != NULL) {
		wfile = fopen(w_opt, "w");
		if (wfile == NULL) {
			perror(w_opt);
			exit (1);
		}
	}
	u = 0;
	v = 0;

	while (1) {
		i = VSL_NextLog(vd, &p);
		if (i < 0)
			break;
		if (i == 0) {
			if (w_opt == NULL) {
				if (++v == 100)
					clean_order();
				fflush(stdout);
			} else if (++v == 100) {
			
				/* Not sure if needed.
				*
				*fflush(wfile);
				*/
				
				printf("\n Inside the ++v==100 and w_opt is set.\n");
				}
			usleep(50000);
			continue;
		}
		v = 0;
		
		/* XXX probably wanna throw this out. Not sure when needed.
		if (wfile != NULL) {
			i = fwrite(p, 4 + p[1], 1, wfile);
			if (i != 1)
				perror(w_opt);
			u++;
			if (!(u % 1000)) {
				printf("%u\r", u);
				fflush(stdout);
			}
			continue;
		}
		*/
		
		extended_log_format(p, w_opt);
		
	
	//printf("\n");
	}
	
	clean_order();
	return (0);
}
