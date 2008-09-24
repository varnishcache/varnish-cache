CFLAGS := -O3 -g
# See source code comments to avoid memory leaks when enabling MALLOC_MAG.
#CPPFLAGS := -DMALLOC_PRODUCTION -DMALLOC_MAG
CPPFLAGS := -DMALLOC_PRODUCTION

all: libjemalloc.so.0 libjemalloc_mt.so.0

jemalloc_linux_mt.o: jemalloc_linux.c
	gcc $(CFLAGS) -c -DPIC -fPIC $(CPPFLAGS) -D__isthreaded=true -o $@ $+

jemalloc_linux.o: jemalloc_linux.c
	gcc $(CFLAGS) -c -DPIC -fPIC $(CPPFLAGS) -D__isthreaded=false -o $@ $+

libjemalloc_mt.so.0: jemalloc_linux_mt.o
	gcc -shared -lpthread -o $@ $+
	ln -sf $@ libjemalloc_mt.so

libjemalloc.so.0: jemalloc_linux.o
	gcc -shared -lpthread -o $@ $+
	ln -sf $@ libjemalloc.so

clean:
	rm -f *.o *.so.0 *.so
