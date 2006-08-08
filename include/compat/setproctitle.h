/*
 * $Id$
 */

#ifndef COMPAT_SETPROCTITLE_H_INCLUDED
#define COMPAT_SETPROCTITLE_H_INCLUDED

#ifndef HAVE_SETPROCTITLE
void setproctitle(const char *fmt, ...);
#endif

#endif
