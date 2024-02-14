/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Stephane Cance <stephane.cance@varnish-software.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Varnish Synchronisation devices.
 */
#ifdef VSYNC_H_INCLUDED
#  error "vsync.h included multiple times"
#endif /* VSYNC_H_INCLUDED */
#define VSYNC_H_INCLUDED

#include <pthread.h>

/**********************************************************************
 * Assertions
 */

#define VSYNC_assert(e)							\
    do {								\
	    if (!(e))							\
		    VAS_Fail((func), (file), (line), #e, VAS_ASSERT);	\
    } while (0)

#define VSYNC__PTOK(e, var)						\
    do {								\
	    int var = (e);						\
	    if (!var)							\
		    break;						\
	    errno = var;						\
	    VAS_Fail((func), (file), (line), #e" failed", VAS_WRONG);	\
    } while (0)

#define VSYNC_PTOK(e)	VSYNC__PTOK((e), VUNIQ_NAME(_pterr))

/**********************************************************************
 * Event instrumentation.
 */

struct VSC_lck;
struct VSC_cond;

enum vsync_mtx_event {
    VSYNC_MTX_INIT,
    VSYNC_MTX_FINI,
    VSYNC_MTX_LOCK,
    VSYNC_MTX_UNLOCK,
};

enum vsync_cond_event {
    VSYNC_COND_INIT,
    VSYNC_COND_FINI,
    VSYNC_COND_SIGNAL,
    VSYNC_COND_BROADCAST,
    VSYNC_COND_WAIT_START,
    VSYNC_COND_WAIT_END,
};

typedef void vsync_mtx_event_f(const char *func, const char *file,
    int line, enum vsync_mtx_event evt, struct VSC_lck *vsc);
typedef void vsync_cond_event_f(const char *func, const char *file,
    int line, enum vsync_cond_event evt, struct VSC_cond *vsc_cond,
    struct VSC_lck *vsc_lck, vtim_mono *start_atp);

extern vsync_mtx_event_f *VSYNC_mtx_event_func;
extern vsync_cond_event_f *VSYNC_cond_event_func;

#define VSYNC_mtx_event(evt, vsc)					    \
    VSYNC__mtx_event((func), (file), (line), VSYNC_MTX_##evt, (vsc))
#define VSYNC_cond_event(evt, vsccond, vscmtx, at)			    \
    VSYNC__cond_event((func), (file), (line), VSYNC_COND_##evt, (vsccond),  \
	(vscmtx), (at))

static inline void
VSYNC__mtx_event(const char *func, const char *file, int line,
    enum vsync_mtx_event evt, struct VSC_lck *vsc)
{
	if ((vsc != NULL) && (VSYNC_mtx_event_func != NULL))
		VSYNC_mtx_event_func(func, file, line, evt, vsc);
}

static inline void
VSYNC__cond_event(const char *func, const char *file, int line,
    enum vsync_cond_event evt, struct VSC_cond *vsccond, struct VSC_lck *vscmtx,
    vtim_mono *start_atp)
{
	if ((vsccond != NULL) && (VSYNC_cond_event_func != NULL)) {
		VSYNC_cond_event_func(func, file, line, evt, vsccond, vscmtx,
		    start_atp);
	}
}

/**********************************************************************
 * Mutex
 */
struct vsync_mtx {
	pthread_mutex_t	mtx;
	struct VSC_lck  *vsc;
	unsigned	held;
	pthread_t	owner;
};

#define VSYNC_MTX_INITIALIZER	    { .mtx = PTHREAD_MUTEX_INITIALIZER }

#define VSYNC_mtx_init(m, vsc)						\
    VSYNC_mtx__init((m), (vsc), __func__, __FILE__, __LINE__)
#define VSYNC_mtx_fini(m)						\
    VSYNC_mtx__fini((m), __func__, __FILE__, __LINE__)

#define VSYNC_mtx_lock(m)						\
    VSYNC_mtx__lock((m), __func__, __FILE__, __LINE__)
#define VSYNC_mtx_unlock(m)						\
    VSYNC_mtx__unlock((m), __func__, __FILE__, __LINE__)
#define VSYNC_mtx_trylock(m)						\
    VSYNC_mtx__trylock((m), __func__, __FILE__, __LINE__)
#define VSYNC_mtx_timedlock(m, realtim)					\
    VSYNC_mtx__timedlock((m), (realtim), __func__, __FILE__, __LINE__)

#define VSYNC_mtx_assert_held(m)					\
    VSYNC_mtx__assert_held((m), __func__, __FILE__, __LINE__)

static inline void
VSYNC_mtx__init(struct vsync_mtx *m, struct VSC_lck *vsc, const char *func,
    const char *file, int line)
{
	VSYNC_assert(m != NULL);
	VSYNC_PTOK(pthread_mutex_init(&m->mtx, /* attr: */NULL));
	m->held = 0;
	m->vsc = vsc;

	VSYNC_mtx_event(INIT, m->vsc);
}

static inline void
VSYNC_mtx__fini(struct vsync_mtx *m, const char *func, const char *file,
    int line)
{
	VSYNC_assert(m != NULL);
	VSYNC_assert(!m->held);
	VSYNC_PTOK(pthread_mutex_destroy(&m->mtx));

	VSYNC_mtx_event(FINI, m->vsc);

	m->vsc = NULL;
}

static inline void
VSYNC_mtx__lock(struct vsync_mtx *m, const char *func, const char *file,
    int line)
{
	VSYNC_assert(m != NULL);
	VSYNC_PTOK(pthread_mutex_lock(&m->mtx));

	VSYNC_assert(!m->held);
	m->held = 1;
	m->owner = pthread_self();

	VSYNC_mtx_event(LOCK, m->vsc);
}

static inline void
VSYNC_mtx__assert_held(struct vsync_mtx *m, const char *func,
    const char *file, int line)
{
	VSYNC_assert(m != NULL);
	VSYNC_assert(m->held);
	VSYNC_assert(pthread_equal(m->owner, pthread_self()));
}

static inline void
VSYNC_mtx__unlock(struct vsync_mtx *m, const char *func, const char *file,
    int line)
{
	VSYNC_mtx_event(UNLOCK, m->vsc);

	VSYNC_mtx__assert_held(m, func, file, line);
	m->held = 0;
	VSYNC_PTOK(pthread_mutex_unlock(&m->mtx));
}

static inline unsigned
VSYNC_mtx__trylock(struct vsync_mtx *m, const char *func, const char *file,
    int line)
{
	int err;

	VSYNC_assert(m != NULL);
	err = pthread_mutex_trylock(&m->mtx);
	if (err == EBUSY)
		return (0);
	VSYNC_PTOK(err);

	VSYNC_assert(!m->held);
	m->held = 1;
	m->owner = pthread_self();

	VSYNC_mtx_event(LOCK, m->vsc);

	return (1);
}

static inline unsigned
VSYNC_mtx__timedlock(struct vsync_mtx *m, vtim_real realtim, const char *func,
    const char *file, int line)
{
	struct timespec ts;
	int err;

	ts.tv_sec = (time_t)realtim;
	ts.tv_nsec = (realtim - (vtim_real)ts.tv_sec) * 1e9;
	VSYNC_assert(ts.tv_sec >= 0);
	VSYNC_assert(ts.tv_nsec >= 0);

	VSYNC_assert(m != NULL);
	err = pthread_mutex_timedlock(&m->mtx, &ts);

	if (err == ETIMEDOUT)
		return (0);
	VSYNC_PTOK(err);

	VSYNC_assert(!m->held);
	m->held = 1;
	m->owner = pthread_self();

	VSYNC_mtx_event(LOCK, m->vsc);

	return (1);
}

/**********************************************************************
 * Condition
 */

struct vsync_cond {
	pthread_cond_t	cond;
	clockid_t	clock_id;
	struct VSC_cond	*vsc;
};

#define VSYNC_COND_INITIALIZER						\
    {									\
	    .cond = PTHREAD_COND_INITIALIZER,				\
	    .clock_id = CLOCK_REALTIME,					\
    }

#define VSYNC_cond_init(c, clock_id, vsc)				\
    VSYNC_cond__init((c), (clock_id), (vsc), __func__,	__FILE__, __LINE__)

#define VSYNC_cond_fini(c)						\
    VSYNC_cond__fini((c), __func__, __FILE__, __LINE__)

#define VSYNC_cond_broadcast(c)						\
    VSYNC_cond__broadcast((c), __func__, __FILE__, __LINE__)

#define VSYNC_cond_signal(c)						\
    VSYNC_cond__signal((c), __func__, __FILE__, __LINE__)

#define VSYNC_cond_wait(c, m)						\
    VSYNC_cond__wait((c), (m), __func__, __FILE__, __LINE__)

/* wait until deadline */
#define VSYNC_cond_timedwait_real(c, m, tim)				\
    VSYNC_cond__timedwait_real((c), (m), (tim), __func__, __FILE__, __LINE__)
#define VSYNC_cond_timedwait_mono(c, m, tim)				\
    VSYNC_cond__timedwait_mono((c), (m), (tim), __func__, __FILE__, __LINE__)

/* wait until deadline or forever if deadline is INF */
#define VSYNC_cond_wait_until_real(c, m, tim)				\
    VSYNC_cond__wait_until_real((c), (m), (tim), __func__, __FILE__, __LINE__)
#define VSYNC_cond_wait_until_mono(c, m, tim)				\
    VSYNC_cond__wait_until_mono((c), (m), (tim), __func__, __FILE__, __LINE__)


void VSYNC_cond_clock_init(pthread_cond_t *cond, clockid_t *idp,
    const char *func, const char *file, int line);

static inline void
VSYNC_cond__init(struct vsync_cond *c, clockid_t clock_id, struct VSC_cond *vsc,
    const char *func, const char *file, int line)
{
	VSYNC_assert(c != NULL);

	c->clock_id = clock_id;
	VSYNC_cond_clock_init(&c->cond, &c->clock_id, func, file, line);

	c->vsc = vsc;

	VSYNC_cond_event(INIT, c->vsc, NULL, NULL);
}

static inline void
VSYNC_cond__fini(struct vsync_cond *c, const char *func, const char *file,
    int line)
{
	VSYNC_assert(c != NULL);

	VSYNC_PTOK(pthread_cond_destroy(&c->cond));

	VSYNC_cond_event(FINI, c->vsc, NULL, NULL);

	c->vsc = NULL;
}

static inline void
VSYNC_cond__broadcast(struct vsync_cond *c, const char *func, const char *file,
    int line)
{
	VSYNC_assert(c != NULL);

	VSYNC_PTOK(pthread_cond_broadcast(&c->cond));

	VSYNC_cond_event(BROADCAST, c->vsc, NULL, NULL);
}

static inline void
VSYNC_cond__signal(struct vsync_cond *c, const char *func, const char *file,
    int line)
{
	VSYNC_assert(c != NULL);

	VSYNC_PTOK(pthread_cond_signal(&c->cond));

	VSYNC_cond_event(SIGNAL, c->vsc, NULL, NULL);
}

static inline void
VSYNC_cond__wait(struct vsync_cond *c, struct vsync_mtx *m, const char *func,
    const char *file, int line)
{
	vtim_mono tm;;
	int err;

	VSYNC_assert(c != NULL);

	VSYNC_mtx__assert_held(m, func, file, line);

	VSYNC_cond_event(WAIT_START, c->vsc, m->vsc, &tm);

	m->held = 0;

	/*
	 * Although POSIX states that EINTR shall never be returned here, this
	 * has apparently been seen on some buggy implementations. This blindly
	 * makes the assumption that both the state of the mutex and of the
	 * cond remain valid and consistent when this happens, similarly to
	 * the Solaris specific non-POSIX style constructs.
	 */
	err = pthread_cond_wait(&c->cond, &m->mtx);
	VSYNC_assert(err == 0 || err == EINTR);

	VSYNC_assert(!m->held);
	m->held = 1;
	m->owner = pthread_self();

	VSYNC_cond_event(WAIT_END, c->vsc, m->vsc, &tm);
}

static inline unsigned
VSYNC_cond__timedwait(struct vsync_cond *c, struct vsync_mtx *m,
    const struct timespec *abstime, const char *func, const char *file,
    int line)
{
	vtim_mono tm;
	int err;

	VSYNC_assert(c != NULL);

	VSYNC_mtx__assert_held(m, func, file, line);

	VSYNC_cond_event(WAIT_START, c->vsc, m->vsc, &tm);

	m->held = 0;

	err = pthread_cond_timedwait(&c->cond, &m->mtx, abstime);

	VSYNC_assert(!m->held);
	m->held = 1;
	m->owner = pthread_self();

	VSYNC_cond_event(WAIT_END, c->vsc, m->vsc, &tm);

	if (err == ETIMEDOUT)
		return (0);

	/* See VSYNC_cond__wait about EINTR support */
	VSYNC_assert(err == 0 || err == EINTR);

	return (1);
}

static inline unsigned
VSYNC_cond__timedwait_real(struct vsync_cond *c, struct vsync_mtx *m,
    vtim_real realtim, const char *func, const char *file, int line)
{
	struct timespec ts;

	VSYNC_assert(c != NULL);

	VSYNC_assert(c->clock_id == CLOCK_REALTIME);

	ts.tv_sec = (time_t)realtim;
	ts.tv_nsec = (realtim - (vtim_real)ts.tv_sec) * 1e9;
	VSYNC_assert(ts.tv_sec >= 0);
	VSYNC_assert(ts.tv_nsec >= 0);

	return (VSYNC_cond__timedwait(c, m, &ts, func, file, line));
}

static inline unsigned
VSYNC_cond__wait_until_real(struct vsync_cond *c, struct vsync_mtx *m,
    vtim_real deadline, const char *func, const char *file, int line)
{
	VSYNC_assert(c != NULL);

	if (isinf(deadline)) {
		VSYNC_cond__wait(c, m, func, file, line);
		return (1);
	}

	return (VSYNC_cond__timedwait_real(c, m, deadline, func, file, line));
}

static inline vtim_real
VSYNC_mono_to_real(vtim_mono monotim)
{
	vtim_real ref_real;
	vtim_mono ref_mono;

	/*
	 * macOS apparently does not support pthread_condattr_setclock(3p)
	 * this is a severely degraded implementation which does not protect
	 * against time jumps and breaks the no drift principle built into
	 * the pthread_cond_timedwait(3p) API, it is assumed that this is
	 * acceptable to callers as long as drift is positive.
	 */
	ref_mono = VTIM_mono();
	ref_real = VTIM_real();

	return monotim - ref_mono + ref_real;
}

static inline unsigned
VSYNC_cond__timedwait_mono(struct vsync_cond *c, struct vsync_mtx *m,
    vtim_mono deadline, const char *func, const char *file, int line)
{
	struct timespec ts;

	VSYNC_assert(c != NULL);
	VSYNC_assert(!isinf(deadline));

	/*
	 * macOS apparently does not support pthread_condattr_setclock(3p)
	 * meaning that a cond required to be monotonic may end up being
	 * instrumented with the realtime clock.
	 */
	if (c->clock_id == CLOCK_REALTIME) {
		return (VSYNC_cond__timedwait_real(c, m,
		    VSYNC_mono_to_real(deadline), func, file, line));
	}

	VSYNC_assert(c->clock_id == CLOCK_MONOTONIC);

	ts.tv_sec = (time_t)deadline;
	ts.tv_nsec = (deadline - (vtim_mono)ts.tv_sec) * 1e9;
	VSYNC_assert(ts.tv_sec >= 0);
	VSYNC_assert(ts.tv_nsec >= 0);

	return (VSYNC_cond__timedwait(c, m, &ts, func, file, line));
}

static inline unsigned
VSYNC_cond__wait_until_mono(struct vsync_cond *c, struct vsync_mtx *m,
    vtim_mono deadline, const char *func, const char *file, int line)
{
	VSYNC_assert(c != NULL);

	if (isinf(deadline)) {
		VSYNC_cond__wait(c, m, func, file, line);
		return (1);
	}

	return (VSYNC_cond__timedwait_mono(c, m, deadline, func, file, line));

}

/**********************************************************************
 * Cleanups
 */

#ifndef VSYNC_KEEP_ASSERTS
# undef VSYNC_assert
# undef VSYNC__PTOK
# undef VSYNC_PTOK
#endif /* VSYNC_KEEP_ASSERTS */

#undef VSYNC_event
