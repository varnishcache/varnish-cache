/*
 * $Id$
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
#include <vsb.h>
#include <vis.h>
#include <time.h>

#include "shmlog.h"
#include "varnishapi.h"


/* Ordering-----------------------------------------------------------*/


/* Adding a struct to hold the data for the logline
 *
 */

struct logline {
	char df_h[4 * (3 + 1)]; // Datafield for %h (IP adress)
	unsigned char *df_r; // Datafield for %r (Request)

};

/* We make a array of pointers to vsb's. Sbuf is a string buffer.
 * * The buffer can be made/extended/cleared etc. through a API.
 * * The array is 65536 long because we will use sessionid as key.
 * *
 * */

static struct vsb      *ob[65536];
static struct logline	ll[65536];


/*
* Clean order is called once in a while. It clears all the sessions that 
* where never finished (SLT_SessionClose). Because the data is not complete
* we disregard the data.
*
*/

static void
clean_order(void)
{
	unsigned u;

	for (u = 0; u < 65536; u++) {
		if (ob[u] == NULL)
			continue;
		vsb_finish(ob[u]);
		vsb_clear(ob[u]);
	}
}

static void 
extended_log_format(unsigned char *p, char *w_opt)
{
	unsigned u,v,w;

	// Used for getting IP.
	unsigned char *tmpPtr;
	int j;

	u = (p[2] << 8) | p[3];
	if (ob[u] == NULL) {
		ob[u] = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
		assert(ob[u] != NULL);
	}
	
	v = 0;
	w = 0;

	switch (p[0]) {

	case SLT_SessionOpen:

		// We catch the IP adress of the session.
		// We also catch IP in SessionReuse because we can not always
		// be sure we see a SessionOpen when we start logging.

		tmpPtr = strchr(p + 4, ' ');
                j = strlen(p + 4) - strlen(tmpPtr);                // length of IP
                strncpy(ll[u].df_h, p + 4, j);
                ll[u].df_h[j] = '\0'; // put on a NULL at end of buffer.
                //printf("New session [%d]: %s \n",u, ll[u].df_h);

		break;

	case SLT_RxRequest:

		break;

	case SLT_RxURL:
		
		break;

	case SLT_RxProtocol:
		
		break;

	case SLT_RxHeader:
			
		break;

	case SLT_ReqServTime:

		break;

	case SLT_TxStatus:

		break;
	
	case SLT_Length:

		break;

	case SLT_SessionClose:

		// Session is closed, we clean up things. But do not write.

		//printf("Session close [%d]\n", u);
		
		v = 1;


		break;

	case SLT_SessionReuse:

		// It's in SessionReuse we wrap things up.
		// SessionClose is not suited to use for a write, but to clean up.

		// Catch IP if not already done.
		
		if (ll[u].df_h[0] == '\0'){
			// We don't have IP, fetch it.
			
			tmpPtr = strchr(p + 4, ' ');
		        j = strlen(p + 4) - strlen(tmpPtr);                // length of IP
		        strncpy(ll[u].df_h, p + 4, j);
	                ll[u].df_h[j] = '\0'; // put on a NULL at end of buffer.
			printf("Got IP from Reuse [%d] : %s\n", u, ll[u].df_h);
		}

		//printf("Session reuse [%d]\n", u);

		w = 1;

		break;

	default:

		break;
	}

	if (v) {
		// We have a SessionClose. Lets clean.
		//
		// Clean IP adress
		ll[u].df_h[0] = '\0';
										
	}

	if (w) {
		// We have a SessionReuse. Lets print the logline
		//
		
		printf("%s ", ll[u].df_h);
		printf("\n");
	}
	
	
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
	char *w_opt = NULL;
	FILE *wfile = NULL;
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
				if (++v == 100){
					clean_order();
					fflush(stdout);
				}
			} 
			usleep(50000);
			continue;
		}
		v = 0;
		
		extended_log_format(p, w_opt);
	}
	
	clean_order();
	return (0);
}

