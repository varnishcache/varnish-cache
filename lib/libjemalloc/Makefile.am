# See source code comments to avoid memory leaks when enabling MALLOC_MAG.
#CPPFLAGS := -DMALLOC_PRODUCTION -DMALLOC_MAG
AM_CPPFLAGS := -DMALLOC_PRODUCTION

#all: libjemalloc.so.0 libjemalloc_mt.so.0

noinst_LTLIBRARIES = libjemalloc_mt.la

libjemalloc_mt_la_LIBADD = ${PTHREAD_LIBS}
libjemalloc_mt_la_LDFLAGS = -static
libjemalloc_mt_la_CFLAGS = -D__isthreaded=true

libjemalloc_mt_la_SOURCES = jemalloc_linux.c \
	rb.h

EXTRA_DIST = malloc.3 \
	malloc.c \
	README
