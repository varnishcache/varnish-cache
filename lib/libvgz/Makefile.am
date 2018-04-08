#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

noinst_LIBRARIES = libvgz.a

libvgz_a_CFLAGS = -D_LARGEFILE64_SOURCE=1 -DZLIB_CONST \
	$(libvgz_extra_cflags) @SAN_CFLAGS@

libvgz_a_SOURCES = \
	adler32.c \
	crc32.c \
	crc32.h \
	deflate.c \
	deflate.h \
	gzguts.h \
	inffast.c \
	inffast.h \
	inffixed.h \
	inflate.c \
	inflate.h \
	inftrees.c \
	inftrees.h \
	trees.c \
	trees.h \
	zconf.h \
	vgz.h \
	zutil.c \
	zutil.h
