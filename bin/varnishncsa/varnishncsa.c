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
#include <time.h>

#include "compat/vis.h"

#include "vsb.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"


/* Ordering-----------------------------------------------------------*/


/* Adding a struct to hold the data for the logline
 *
 */

struct logline {
	char df_h[4 * (3 + 1)]; // Datafield for %h (IP adress)
	int df_hfini;	// Set to 1 when a SessionClose is seen.
	unsigned char *df_r; // Datafield for %r (Request)
	int df_rfini;	// Set to 1 when a ReqServTime has come.
	unsigned char *df_s; // Datafield for %s, Status
	unsigned char *df_b; // Datafield for %b, Bytes
	struct tm *logline_time; // Datafield for %t
	unsigned char *df_R; // Datafield for %{Referer}i
	unsigned char *df_U; // Datafield for %{User-agent}i
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

	// Used for requesttime.
	char *tmpPtra = NULL;
        char *tmpPtrb = NULL;
        char *tmpPtrc = NULL;
        int timesec = 0; // Where we store the utime for request as int.
        char temp_time[27]; // Where we store the string we take from the log
        time_t req_time; // Timeobject used for making the requesttime.
	int i;


	u = (p[2] << 8) | p[3];
	if (ob[u] == NULL) {
		ob[u] = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
		assert(ob[u] != NULL);
	}
	
	v = 0;
	w = 0;
	i = 0;
	j = 0;
	ll[u].df_rfini = 0;
	ll[u].df_hfini = 0;

	switch (p[0]) {

	case SLT_SessionOpen:

		// We catch the IP adress of the session.
		// We also catch IP in SessionReuse because we can not always
		// be sure we see a SessionOpen when we start logging.

		tmpPtr = strchr(p + 4, ' ');
                j = strlen(p + 4) - strlen(tmpPtr);                // length of IP
                strncpy(ll[u].df_h, p + 4, j);
                ll[u].df_h[j] = '\0'; // put on a NULL at end of buffer.
                printf("New session [%d]: %s \n",u, ll[u].df_h);

		break;

	case SLT_ReqStart:

		// We use XID to catch that a new request is comming inn.

		break;

	case SLT_RxRequest:

		vsb_clear(ob[u]);

		if (p[1] >= 4 && !strncasecmp((void *)&p[4], "HEAD",4)){
			vsb_bcat(ob[u], p + 4, strlen(p + 4));
			//printf("Got a HEAD\n");
		}
	
		else if (p[1] >= 4 && !strncasecmp((void *)&p[4], "POST",4)){
			vsb_bcat(ob[u], p + 4, strlen(p + 4));
			//printf("Got a POST\n");
		}
		
		else if (p[1] >= 3 && !strncasecmp((void *)&p[4], "GET",3)){
			vsb_bcat(ob[u], p + 4, strlen(p + 4));
			//printf("Got a GET\n");
		}
		
		else {
			vsb_bcat(ob[u], p + 4, strlen(p + 4));
			//printf("Got something other than HEAD, POST, GET\n");
		}
		
	break;

	case SLT_RxURL:

		vsb_cat(ob[u], " ");
		vsb_bcat(ob[u], p + 4, strlen(p + 4));

		break;

	case SLT_RxProtocol:
		
		vsb_cat(ob[u], " ");
                vsb_bcat(ob[u], p + 4, strlen(p + 4));

		break;

	case SLT_TxStatus:
		
		ll[u].df_s = strdup(p + 4);

		break;
	
	case SLT_RxHeader:
		/*
		if (p[1] >= 11 && !strncasecmp((void *)&p[4], "user-agent:",11)){
                        ll[u].df_U = strdup(p + 4);
                }
                if (p[1] >= 8 && !strncasecmp((void *)&p[4], "referer:",8)){
                        ll[u].df_R = strdup(p + 4);
                }
		else if (ll[u].df_R == NULL){
                        ll[u].df_R = strdup(p + 4);
                        ll[u].df_R[0] = '-';
                        ll[u].df_R[1] = '\0';
                }
		*/
		break;
	

	case SLT_ReqEnd:

		// We use ReqServTime to find how the time the request was delivered
		// also to define that a request is finished.
		/*	
		tmpPtra =  strdup(p + 4);
		tmpPtrb = malloc(strlen(p + 4));
		tmpPtrc = malloc(strlen(p + 4));
		temp_time[0] = '\0';

		for ( tmpPtrb = strtok(tmpPtra," "); tmpPtrb != NULL; tmpPtrb = strtok(NULL, " ")){
			if (i == 1){
				// We have the right time
			   	tmpPtra = tmpPtrb;
			}
			//printf("ReqServTime number %d: %s\n", i, tmpPtrb);
		
	                i++;
	         }
		tmpPtrc = strchr(tmpPtra, '.');
	        j = strlen(tmpPtrc);                // length of timestamp
		//printf("j=%d\n", j);
		strncpy(temp_time, tmpPtra, j);
		temp_time[j] = '\0';
		//printf("inten: %s\n",temp_time);
		timesec = atoi(temp_time);
		req_time = timesec;
                ll[u].logline_time = localtime(&req_time);
                strftime (temp_time, 50, "[%d/%b/%Y:%X %z] ", ll[u].logline_time);
		*/
		ll[u].df_rfini = 1;
		printf("ReqServTime [%d]\n", u);

		break;

	case SLT_Length:

		// XXX ask DES or PHK about this one. Am I overflowing?

		ll[u].df_b = strdup(p + 4);
		/*
                if (!atoi(ll[u].df_b)){
	                ll[u].df_b[0] = '-';
        	        ll[u].df_b[1] = '\0';
                }
		*/

		break;

	case SLT_SessionClose:

		// Session is closed, we clean up things. But do not write.

		printf("Session close [%d]\n", u);
		
		ll[u].df_hfini = 1;

		break;

	case SLT_SessionReuse:

		// We use SessionReuse to catch the IP adress of a session that has already
		// started with a SessionOpen that we did not catch.
		// Other than that it is not used.

		// Catch IP if not already done.
		
		if (ll[u].df_h[0] == '\0'){
			// We don't have IP, fetch it.
			
			tmpPtr = strchr(p + 4, ' ');
		        j = strlen(p + 4) - strlen(tmpPtr);                // length of IP
		        strncpy(ll[u].df_h, p + 4, j);
	                ll[u].df_h[j] = '\0'; // put on a NULL at end of buffer.
			printf("Got IP from Reuse [%d] : %s\n", u, ll[u].df_h);
		}

		printf("Session reuse [%d]\n", u);

		break;

	default:

		// printf("DEBUG: %s\n", p+4);

		break;
	}


	if (ll[u].df_rfini) {
		// We have a ReqServTime. Lets print the logline
		// and clear variables that are different for each request.
		//

		if (ll[u].df_h[0] == '\0'){
		printf("Tom IP \n");		
		}
		else{	
			printf("[%d] %s - -  ", u, ll[u].df_h );
			vsb_finish(ob[u]);
			printf("\"%s\"", vsb_data(ob[u]));
			printf(" %s %s \"%s\" \"%s\"", ll[u].df_s, ll[u].df_b,  ll[u].df_R,  ll[u].df_U);
			printf("\n");
		}

		vsb_finish(ob[u]);
               	vsb_clear(ob[u]);
		ll[u].df_rfini = 0;

		// Clear the TxStaus

		if (ll[u].df_s != NULL){
			free(ll[u].df_s);
			printf("Freed df_s [%d]\n", u);
		}

		if (ll[u].df_b != NULL){
			free(ll[u].df_b);
			printf("Freed df_b [%d]\n", u);
		}

		// Clean User-Agent and Referer
		if (ll[u].df_U != NULL){
			free(ll[u].df_U);
			printf("Freed df_U [%d]\n", u);
		}
		
		if (ll[u].df_R != NULL){
			free(ll[u].df_R);
			printf("Freed df_R [%d]\n", u);
		}

		// Clean up ReqEnd/Time variables
		
		if (tmpPtra != NULL){
			free(tmpPtra);
			printf("Freed tmpPtra [%d]\n", u);
		}

		if (tmpPtrb != NULL){
			free(tmpPtrb);
			printf("Freed tmpPtrb [%d]\n", u);
		}
		if (tmpPtrc != NULL){
			free(tmpPtrc);
			printf("Freed tmpPtrc [%d]\n", u);
		}
		temp_time[0] = '\0';	
		

		if (ll[u].df_hfini) {
			// We have a SessionClose. Lets clean data.
			//
			// Clean IP adress
			ll[u].df_h[0] = '\0';
			printf("Clearer [%d]\n", u);
										
		}




	}
	
	
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishncsa [-V] [-w file] [-r file]\n");
	exit(1);
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
		case 'V':
			varnish_version("varnishncsa");
			exit(0);
		case 'w':
			w_opt = optarg;
			break;
		default:
			usage();
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

