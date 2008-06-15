PROG		=	vtc

SRCS		+=	vtc.c
SRCS		+=	vtc_server.c
SRCS		+=	vtc_client.c
SRCS		+=	vtc_vcl.c
SRCS		+=	vtc_stats.c
SRCS		+=	vtc_varnish.c

CFLAGS		+= 	-g

NO_MAN		= sorry

WARNS		?=	6

.include <bsd.prog.mk>

test:		${PROG}
		./${PROG} ${.CURDIR}/t000.vtc
