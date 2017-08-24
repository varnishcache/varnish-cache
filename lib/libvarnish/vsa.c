/*-
 * Copyright (c) 2013-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Struct sockaddr_* is not even close to a convenient API.
 *
 * These functions try to mitigate the madness, at the cost of actually
 * knowing something about address families.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "vdef.h"
#include "vas.h"
#include "vsa.h"
#include "vrt.h"
#include "miniobj.h"

/*
 * Struct sockaddr{|_in|_in6|_storage} is absolutely the worst data
 * structure I have ever seen gold-plated in international standards.
 *
 * Network addresses have multiple different forms, many fewer today
 * than in last century, but imagine that in addition to IPv4 and IPv6
 * we had 40 other protocols.  Actually, you don't need to imagine that
 * just count the AF_* macros in /usr/include/sys/socket.h.
 *
 * So what do we pass the kernel API for an address to bind(2), connect(2) &
 * listen(2) etc. etc ?
 *
 * We could define a struct which is big enough to hold any and all
 * of these addresses.  That would make it a fixed size argument.
 * obviously the struct would have to be something like:
 *	struct bla {
 *		int family;
 *		char address[MAX_ADDR_LEN];
 *	}
 * and MAX_ADDR_LEN would have to be quite large, 128 byte or so.
 *
 * Back in last century that was TOTALLY unacceptable waste of space.
 *
 * The way which was chosen instead, was to make a "generic" address,
 * and have per protocol "specific" addresses, and pass the length
 * argument explicitly to the KPI functions.
 *
 * The generic address was called "struct sockaddr", and the specific
 * were called "struct sockaddr_${whatever}".  All of these must have
 * a "family" field as first element, so the kernel can figure out
 * which protocol it is.
 *
 * The generic struct sockaddr was made big enough for all protocols
 * supported in the kernel, so it would have different sizes depending
 * on your machine and kernel configuration.
 *
 * However, that allowed you to write protocol-agnostic programs, by
 * using "struct sockaddr" throughout, and relying on libray APIs for
 * things like name to address (and vice versa) resolution, and since
 * nobody were in the business of shipping random UNIX binaries around
 * the lack of binary portability didn't matter.
 *
 * Along the way the BSD people figured out that it was a bother
 * to carry the length argument separately, and added that to the
 * format of sockaddr, but other groups found this unclean, as
 * the length was already an explicit paramter.
 *
 * The net result of this is that your "portable" code, must take
 * care to handle the "sa_len" member on kernels which have it,
 * while still tracking the separate length argument for all other
 * kernels.
 *
 * Needless to say, there were no neat #define to tell you which
 * was which, so each programmer found a different heuristic to
 * decide, often not understanding it fully, which caused the kind
 * of portability issues which lead to the autocrap tools.
 *
 * Then all the other protocols died, we were left with IP and
 * life were good, the dot-com madness multiplied the IT-business
 * by a factor 1000, by making any high-school student who had
 * programmed PERL for 6 weeks a "senior web-programmer".
 *
 * Next IPv6 happened, in a rush even, (no seriously, I'm not kidding!),
 * and since IPv6 addresses were HUGE, like 16 bytes HUGE, the generic
 * struct sockaddr was not increased in size.
 *
 * At least "not yet", because it would break all the shitty code written
 * by the dot-com generation.
 *
 * Nobody used IPv6 anyway so that didn't matter that much.
 *
 * Then people actually started using IPv6 and its struct sockaddr_in6,
 * and realized that all the code which used "struct sockaddr" to allocate
 * space at compile time were broken.
 *
 * Some people took to using sockaddr_in6, since that was known to
 * be big enough for both IPv4 and IPv6, but "purist" found that
 * ugly and "prone to future trouble".
 *
 * So instead they came up with a "clean solution":  The added
 * "struct sockaddr_storage" which is defined to be "Large enough
 * to accommodate all supported protocol-specific address structures".
 *
 * Since we cannot possibly know what zany protocols will exist in
 * the future, and since some people think that we will add future
 * protocols, while retaining ABI compatibility, (totally overlooking
 * the fact that no code for name-resolution supports that) it is
 * usually defined so it can cope with 128 byte addresses.
 *
 * Does that ring a bell ?
 *
 * Only, not quite:  Remember that all APIs require you to track
 * the address and the length separately, so you only get the
 * size of the specific protocols sockaddr_${whatever} from API
 * functions, not a full sockaddr_storage, and besides the
 * prototype for the KPI is still "struct sockaddr *", so you
 * cannot gain C type-safety back by using sockaddr_storage
 * as the "generic network address" type.
 *
 * So we have come full circle, while causing maximum havoc along
 * the way and for the forseeable future.
 *
 * Do I need to tell you that static code analysis tools have a
 * really hard time coping with this, and that they give a lot of
 * false negatives which confuse people ?
 *
 * I have decided to try to contain this crap in this single
 * source-file, with only minimum leakage into the rest of Varnish,
 * which will only know of pointers to "struct suckaddr", the naming
 * of which is my of the historical narrative above.
 *
 * And you don't need to take my word for this, you can see it all
 * in various #include files on your own system.   If you are on
 * a Solaris derivative, don't miss the beautiful horror hidden in the
 * variant definition of IPv6 addresses between kernel and userland.
 *
 */

struct suckaddr {
	unsigned			magic;
#define SUCKADDR_MAGIC			0x4b1e9335
	sa_family_t			sa_family;
	union {
		struct sockaddr_in		sa4;
		struct sockaddr_in6		sa6;
		const struct sockaddr_un	*sau;
	}				sa;
};

const int vsa_suckaddr_len = sizeof(struct suckaddr);

/*
 * This VRT interface is for the VCC generated ACL code, which needs
 * to know the address family and a pointer to the actual address.
 */

int
VRT_VSA_GetPtr(const struct suckaddr *sua, const unsigned char ** dst)
{

	AN(dst);
	if (sua == NULL)
		return (-1);
	CHECK_OBJ_NOTNULL(sua, SUCKADDR_MAGIC);

	switch (sua->sa_family) {
	case PF_INET:
		assert(sua->sa_family == sua->sa.sa4.sin_family);
		*dst = (const unsigned char *)&sua->sa.sa4.sin_addr;
		return (sua->sa.sa4.sin_family);
	case PF_INET6:
		assert(sua->sa_family == sua->sa.sa6.sin6_family);
		*dst = (const unsigned char *)&sua->sa.sa6.sin6_addr;
		return (sua->sa.sa6.sin6_family);
	case PF_UNIX:
		assert(sua->sa_family == sua->sa.sau->sun_family);
		*dst = (const unsigned char *)sua->sa.sau->sun_path;
		return (sua->sa.sau->sun_family);
	default:
		*dst = NULL;
		return (-1);
	}
}

/*
 * Malloc a suckaddr from a sockaddr of some kind.
 */

struct suckaddr *
VSA_Malloc(const void *s, unsigned  sal, const void *suds)
{
	struct suckaddr *sua = NULL;
	const struct sockaddr *sa = s;
	unsigned l = 0;

	AN(s);
	switch (sa->sa_family) {
	case PF_INET:
		if (sal == sizeof sua->sa.sa4)
			l = sal;
		break;
	case PF_INET6:
		if (sal == sizeof sua->sa.sa6)
			l = sal;
		break;
	default:
		break;
	}
	if (l != 0 || sa->sa_family == PF_UNIX) {
		ALLOC_OBJ(sua, SUCKADDR_MAGIC);
		if (sua == NULL)
			return (NULL);
		sua->sa_family = sa->sa_family;
		if (sa->sa_family != PF_UNIX)
			memcpy(&sua->sa, s, l);
		else
			sua->sa.sau = suds;
	}
	return (sua);
}

/* 'd' SHALL point to vsa_suckaddr_len aligned bytes of storage */
struct suckaddr *
VSA_Build(void *d, const void *s, unsigned sal, const void *suds)
{
	struct suckaddr *sua = d;
	const struct sockaddr *sa = s;
	unsigned l = 0;

	AN(d);
	AN(s);
	switch (sa->sa_family) {
	case PF_INET:
		if (sal == sizeof sua->sa.sa4)
			l = sal;
		break;
	case PF_INET6:
		if (sal == sizeof sua->sa.sa6)
			l = sal;
		break;
	default:
		break;
	}
	if (l != 0 || sa->sa_family == PF_UNIX) {
		memset(sua, 0, sizeof *sua);
		sua->magic = SUCKADDR_MAGIC;
		sua->sa_family = sa->sa_family;
		if (sa->sa_family != PF_UNIX)
			memcpy(&sua->sa, s, l);
		else
			sua->sa.sau = suds;
		return (sua);
	}
	return (NULL);
}

/*
 * 'uds' is a PF_UNIX suckaddr. '*uds_sockaddr' will point to storage for
 * the sockaddr_un that will be "owned" by the caller -- the caller is
 * responsible for freeing it.
 * Allocate the sockaddr_un in *uds_sockaddr, and return a dup of uds that
 * points to the newly allocated sockaddr_un.
 */
struct suckaddr *
VSA_Malloc_UDS(const struct suckaddr *uds, void **uds_sockaddr)
{
	struct suckaddr *sua;

	CHECK_OBJ_NOTNULL(uds, SUCKADDR_MAGIC);
	assert(uds->sa_family == PF_UNIX);
	AN(uds_sockaddr);

	*uds_sockaddr = malloc(sizeof *uds->sa.sau);
	if (*uds_sockaddr == NULL)
		return (NULL);
	memcpy(*uds_sockaddr, uds->sa.sau, sizeof *uds->sa.sau);

	ALLOC_OBJ(sua, SUCKADDR_MAGIC);
	if (sua == NULL)
		return (NULL);
	sua->sa_family = PF_UNIX;
	sua->sa.sau = *uds_sockaddr;
	return (sua);
}

const void *
VSA_Get_Sockaddr(const struct suckaddr *sua, socklen_t *sl)
{

	CHECK_OBJ_NOTNULL(sua, SUCKADDR_MAGIC);
	AN(sl);
	switch (sua->sa_family) {
	case PF_INET:
		*sl = sizeof sua->sa.sa4;
		return (&sua->sa.sa4);
	case PF_INET6:
		*sl = sizeof sua->sa.sa6;
		return (&sua->sa.sa6);
	case PF_UNIX:
		*sl = sizeof *sua->sa.sau;
		return (sua->sa.sau);
	default:
		return (NULL);
	}
}

int
VSA_Get_Proto(const struct suckaddr *sua)
{

	CHECK_OBJ_NOTNULL(sua, SUCKADDR_MAGIC);
	return (sua->sa_family);
}

int
VSA_Sane(const struct suckaddr *sua)
{
	CHECK_OBJ_NOTNULL(sua, SUCKADDR_MAGIC);

	switch (sua->sa_family) {
	case PF_INET:
	case PF_INET6:
	case PF_UNIX:
		return (1);
	default:
		return (0);
	}
}

int
VSA_Compare(const struct suckaddr *sua1, const struct suckaddr *sua2)
{

	CHECK_OBJ_NOTNULL(sua1, SUCKADDR_MAGIC);
	CHECK_OBJ_NOTNULL(sua2, SUCKADDR_MAGIC);
	if (sua1->sa_family == PF_UNIX) {
		if (sua2->sa_family != PF_UNIX)
			return 1;
		return (strcmp(sua1->sa.sau->sun_path, sua2->sa.sau->sun_path));
	}
	return (memcmp(sua1, sua2, vsa_suckaddr_len));
}

/* XXX For UDSen compare the paths. Maybe rename to VSA_Compare_Addr. */

int
VSA_Compare_IP(const struct suckaddr *sua1, const struct suckaddr *sua2)
{

	assert(VSA_Sane(sua1));
	assert(VSA_Sane(sua2));

	if (sua1->sa_family != sua2->sa_family)
		return (-1);

	switch (sua1->sa_family) {
	case PF_INET:
		return (memcmp(&sua1->sa.sa4.sin_addr,
		    &sua2->sa.sa4.sin_addr, sizeof(struct in_addr)));
	case PF_INET6:
		return (memcmp(&sua1->sa.sa6.sin6_addr,
		    &sua2->sa.sa6.sin6_addr, sizeof(struct in6_addr)));
	case PF_UNIX:
		return (strcmp(sua1->sa.sau->sun_path, sua2->sa.sau->sun_path));
	default:
		WRONG("Just plain insane");
	}
	NEEDLESS(return(-1));
}

struct suckaddr *
VSA_Clone(const struct suckaddr *sua)
{
	struct suckaddr *sua2;
	struct sockaddr_un *suds;

	assert(VSA_Sane(sua));
	sua2 = calloc(1, vsa_suckaddr_len);
	XXXAN(sua2);
	memcpy(sua2, sua, vsa_suckaddr_len);
	if (sua->sa_family == PF_UNIX && sua->sa.sau != NULL) {
		suds = calloc(1, sizeof(struct sockaddr_un));
		XXXAN(suds);
		memcpy(suds, sua->sa.sau, sizeof(struct sockaddr_un));
		sua2->sa.sau = suds;
	}
	return (sua2);
}

unsigned
VSA_Port(const struct suckaddr *sua)
{

	CHECK_OBJ_NOTNULL(sua, SUCKADDR_MAGIC);
	switch (sua->sa_family) {
	case PF_INET:
		return (ntohs(sua->sa.sa4.sin_port));
	case PF_INET6:
		return (ntohs(sua->sa.sa6.sin6_port));
	case PF_UNIX:
	default:
		return (0);
	}
}

const char *
VSA_Path(const struct suckaddr *sua)
{

	CHECK_OBJ_NOTNULL(sua, SUCKADDR_MAGIC);
	switch (sua->sa_family) {
	case PF_INET:
	case PF_INET6:
		return (NULL);
	case PF_UNIX:
		return (sua->sa.sau->sun_path);
	default:
		WRONG("invalid sockaddr family");
	}
	NEEDLESS(return(NULL));
}
