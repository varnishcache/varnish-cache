/*
 * $Id$
 */

#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED


void log_info(const char *fmt, ...);
void log_err(const char *fmt, ...);
void log_syserr(const char *fmt, ...);
void log_panic(const char *fmt, ...);
void log_syspanic(const char *fmt, ...);

#define LOG_UNREACHABLE() \
	log_panic("%s(%d): %s(): unreachable code reached", \
	    __FILE__, __LINE__, __func__)

#endif
