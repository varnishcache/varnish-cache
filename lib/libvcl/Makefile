PROG	=	vpc

SRCS	+=	vcl_compile.c
SRCS	+=	vcl_fixed_token.c

NO_MAN	=	yes

WARNS	?=	5

LDADD	+=	-lsbuf

.include <bsd.prog.mk>


test:	${PROG}
	./${PROG} ${.CURDIR}/sample.vcl
	cc -Wall -c _.c

flint:	
	flint flint.lnt -I/usr/include -I. ${SRCS}

distclean:	clean
	
