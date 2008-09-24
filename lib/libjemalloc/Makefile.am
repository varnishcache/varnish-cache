# See source code comments to avoid memory leaks when enabling MALLOC_MAG.
#CPPFLAGS := -DMALLOC_PRODUCTION -DMALLOC_MAG
AM_CPPFLAGS := -DMALLOC_PRODUCTION

#all: libjemalloc.so.0 libjemalloc_mt.so.0

noinst_LTLIBRARIES = libjemalloc_mt.la

libjemalloc_mt_la_LIBADD = ${PTHREAD_LIBS}
libjemalloc_mt_la_LDFLAGS = -version-info 0:0:0 -static
libjemalloc_mt_la_CFLAGS = -D__isthreaded=true

libjemalloc_mt_la_SOURCES = jemalloc_linux.c

#jemalloc_linux.o: jemalloc_linux.c
#	gcc $(CFLAGS) -c -DPIC -fPIC $(CPPFLAGS) -D__isthreaded=false -o $@ $+

#libjemalloc_mt.so.0: jemalloc_linux_mt.o
#	gcc -shared -lpthread -o $@ $+
#	ln -sf $@ libjemalloc_mt.so

#libjemalloc.so.0: jemalloc_linux.o
#	gcc -shared -lpthread -o $@ $+
#	ln -sf $@ libjemalloc.so

#clean:
#	rm -f *.o *.so.0 *.so
