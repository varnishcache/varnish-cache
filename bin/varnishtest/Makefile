PROG		=	vtc

SRCS		+=	vtc.c
SRCS		+=	vtc_server.c

CFLAGS		+= 	-g

NO_MAN		= sorry

WARNS		?=	6

.include <bsd.prog.mk>

test:		${PROG}
		./${PROG} ${.CURDIR}/t000.vtc
