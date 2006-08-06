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
#include <sbuf.h>
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
	// XXX Set to 1 if we have a IP adress. Not sure when to unset.
	// Know for sure when we have a real SessionClose. Probably
	// When we clean also. When we have timeout. Are there any more?
	int v;
	// Set to 1 if we wanna print the loglinestring because we are done
	int w;
	//   unsigned char *df_l; // Datafield for %l
	//   unsigned char *df_u; // Datafield for %u
	struct tm *logline_time; // Datafield for %t
	unsigned char *df_r; // Datafield for %r
	unsigned char *df_s; // Datafield for %s
	unsigned char *df_b; // Datafield for %b
	unsigned char *df_R; // Datafield for %{Referer}i
	unsigned char *df_U; // Datafield for %{User-agent}i
};

/* We make a array of pointers to sbuf's. Sbuf is a string buffer.
 * * The buffer can be made/extended/cleared etc. through a API.
 * * The array is 65536 long because we will use sessionid as key.
 * *
 * */

static struct sbuf      *ob[65536];
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
		sbuf_finish(ob[u]);
		sbuf_clear(ob[u]);
	}
}

static void 
extended_log_format(unsigned char *p, char *w_opt)
{
	unsigned u,v,w;
	int i,j;
	unsigned char *tmpPtr;
	char *tmpPtra;
	char *tmpPtrb;
	char *tmpPtrc;
	int timesec = 0; // Where we store the utime for request as int.
	char temp_time[27]; // Where we store the string we take from the log
	time_t req_time; // Timeobject used for making the requesttime.

	// Variables used to clean memory.
	int cm_h = 0;
	int cm_r = 0;
	int cm_s = 0;
	int cm_b = 0;
	int cm_R = 0;
	int cm_U = 0;




	if (w_opt != NULL){
		// printf(" Has w_opt\n");
	} else {
		//printf(" Does not have w_opt:\n");
	}

	u = (p[2] << 8) | p[3];
	if (ob[u] == NULL) {
		ob[u] = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
		assert(ob[u] != NULL);
	}
	//printf("Hele [%d]: %s %s\n",u, p+4);
	
	i = 0;
	v = 0;
	w = 0;
	ll[u].w = 0;

	switch (p[0]) {

		// XXX remember to check for NULL when strdup, if no allocate
		// XXX also remember to free() after strdup?

	case SLT_SessionOpen:
		// Finding the IP adress when data is: "XXX.XXX.XXX.XXX somenumber"

		tmpPtr = strchr(p + 4, ' ');
	        j = strlen(p + 4) - strlen(tmpPtr);                // length of IP
	        strncpy(ll[u].df_h, p + 4, j);
		ll[u].df_h[j] = '\0'; // put on a NULL at end of buffer.
		//printf("New session [%d]: %s \n",u, ll[u].df_h);
		ll[u].v = 1; // We have IP

		// We have a new session. This is a good place to initialize and
		// clean data from previous sessions with same filedescriptor.
		
		//free(ll[u].df_U);

		break;

	case SLT_RxRequest:
		// XXX: Remember to support more than GET, HEAD and POST.
		// http://rfc.net/rfc2616.html#p51
		//
		// Have to gather together data in SLT_RxRequest, SLT_RxURL, SLT_RxProtocol
		// to build the request, so I use a sbuf.
		
		sbuf_clear(ob[u]);
	
		if (p[1] >= 4 && !strncasecmp((void *)&p[4], "HEAD",4)){
			sbuf_bcat(ob[u], p + 4, strlen(p + 4));
	                //printf("Got a HEAD\n");
	        }

		else if (p[1] >= 4 && !strncasecmp((void *)&p[4], "POST",4)){
			sbuf_bcat(ob[u], p + 4, strlen(p + 4));
		        //printf("Got a POST\n");
		}

		else if (p[1] >= 3 && !strncasecmp((void *)&p[4], "GET",3)){
			sbuf_bcat(ob[u], p + 4, strlen(p + 4));
			//printf("Got a GET\n");
		}

		else {
			sbuf_bcat(ob[u], p + 4, strlen(p + 4));
			//printf("Got something other than HEAD, POST, GET\n");
		}

		break;

	case SLT_RxURL:
		
		sbuf_cat(ob[u], " ");
		sbuf_bcat(ob[u], p + 4, strlen(p + 4));

		break;

	case SLT_RxProtocol:
		
		sbuf_cat(ob[u], " ");
		sbuf_bcat(ob[u], p + 4, strlen(p + 4));

		break;

	case SLT_RxHeader:
			
		if (p[1] >= 11 && !strncasecmp((void *)&p[4], "user-agent:",11)){
			ll[u].df_U = strdup(p + 4);
			cm_U = 1;
		}
		if (p[1] >= 8 && !strncasecmp((void *)&p[4], "referer:",8)){
			ll[u].df_R = strdup(p + 4);
			cm_R = 1;
		}
		else if (ll[u].df_R == NULL){
			ll[u].df_R = strdup(p + 4);
			ll[u].df_R[0] = '-';
			ll[u].df_R[1] = '\0';
			cm_R = 1;
		}

		break;

	case SLT_ReqServTime:

		// First clear temp_time
		temp_time[0] = '\0';

		tmpPtrb =  strdup(p + 4);

		for ( tmpPtra = strtok(tmpPtrb," "); tmpPtra != NULL; tmpPtra = strtok(NULL, " ")){
			//if (i = 1){
				tmpPtrc = tmpPtra;
			//}
			//printf("ReqServTime number %d: %s\n", i, tmpPtra);
			
			i++;
		}
		

		//printf("Br: %s\n",tmpPtrc);
		
		/*
		tmpPtr = strchr(tmpPtrc, '.');
		j = strlen(tmpPtrc) - strlen(tmpPtr);                // length of timestamp
		strncpy(temp_time, tmpPtrc, j);
		temp_time[j] = '\0';
		printf("j: %s",temp_time);
		timesec = atoi(temp_time);
		*/
		timesec = 1;
		req_time = timesec;
		ll[u].logline_time = localtime(&req_time);
		strftime (temp_time, 50, "[%d/%b/%Y:%X %z] ", ll[u].logline_time);
		cm_r = 1;

		break;

	case SLT_TxStatus:

		ll[u].df_s = strdup(p + 4);

		break;
	
	case SLT_Length:

		ll[u].df_b = strdup(p + 4);
		if (!atoi(ll[u].df_b)){
			ll[u].df_b[0] = '-';	
			ll[u].df_b[1] = '\0';
		}

		break;

	case SLT_SessionClose:

		if (p[1] >= 7 && !strncasecmp((void *)&p[4], "timeout",7)){
			// XXX what to do with the timeout?
			// Right now I am gonna just let it pass, and not even clean memory.
			//printf("Timeout...\n");
			//ll[u].w = 1;
		}
		else{

			ll[u].w = 1; // We are done, clean memory

		}

		free(ll[u].df_U);

		break;

	case SLT_SessionReuse:

		// XXX have to catch the IP in the SessionReuse in case
		// We never got the SessionOpen and the client keeps open

		if (ll[u].df_h[0] == '\0'){
			// If we are here, there is a session going on, and we haven't
			// catched the IP in SessionOpen, we "steal" it from SessionReuse.
			//
			tmpPtr = strchr(p + 4, ' ');
			j = strlen(p + 4) - strlen(tmpPtr);                // length of IP
			strncpy(ll[u].df_h, p + 4, j);
			ll[u].df_h[j] = '\0'; // put on a NULL at end of buffer.
			ll[u].v = 1; // We have a IP

		}
		

		ll[u].w = 1; // We are done, clean memory
		
		break;

	default:

		break;
	}

	// Memorycleaner and stringwriter. w is 1 after SLT_SessionClose OR SLT_SessionReuse that
	// do something useful. v is set when we have a real IP adress, somewhere we are getting
	// requests without.
	//
	// XXX Find out why we don't have IP and get rid of v.
	//
	if (ll[u].w && ll[u].v) {
	
		

		printf("%s - - %s", ll[u].df_h, temp_time);
                sbuf_finish(ob[u]);
                printf("\"%s\"", sbuf_data(ob[u]));
                printf(" %s %s \"%s\" \"%s\"\n", ll[u].df_s, ll[u].df_b, ll[u].df_R, ll[u].df_U);
                sbuf_clear(ob[u]);
		ll[u].df_U == NULL;

		

		if (cm_R){
			// Clean the memory for Referer
			free(ll[u].df_R);
		}
		if (cm_U){
			// Clean User-Agent.

			// Clean memory for User-Agent
			free(ll[u].df_U);

			// Initialize User-Agent.
			ll[u].df_U == NULL;
		
		}
		if (cm_r){
			// Clean memory for Date variables
			free(tmpPtrb);
		}
		// XXX We reinitialize the struct logline
		// free(ll[u]);
										
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

