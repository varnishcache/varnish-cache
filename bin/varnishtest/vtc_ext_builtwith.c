/*-
 * Copyright (c) 2008-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * implements builtwith command and defines pkg_version and pkg_branch
 * macros, if PACKAGE_VERSION / PACKAGE_BRANCH are defined
 */

#include "config.h"

#include <sys/wait.h>
#include <sys/socket.h>

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef HAVE_SYS_PERSONALITY_H
#  include <sys/personality.h>
#endif

#include "vtc.h"

#include "vfil.h"
#include "vnum.h"
#include "vtcp.h"
#include "vss.h"
#include "vtim.h"
#include "vus.h"

/* SECTION: builtwith feature
 *
 * Test that the required feature(s) for a test have been compiled in, and skip
 * the test otherwise
 *
 * 64bit
 *        The environment is 64 bits
 * persistent_storage
 *        built with the deprecated persistent storage.
 * coverage
 *        built with code coverage enabled.
 * asan
 *        built with the address sanitizer.
 * msan
 *        built with the memory sanitizer.
 * tsan
 *        built with the thread sanitizer.
 * ubsan
 *        built with the undefined behavior sanitizer.
 * sanitizer
 *        built with a sanitizer.
 * workspace_emulator
 *        built with its workspace emulator.
 *
 * A feature name can be prefixed with an exclamation mark (!) to skip a
 * test if the feature is present.
 */

#if ENABLE_COVERAGE
static const unsigned coverage = 1;
#else
static const unsigned coverage = 0;
#endif

#if ENABLE_ASAN
static const unsigned asan = 1;
#else
static const unsigned asan = 0;
#endif

#if ENABLE_MSAN
static const unsigned msan = 1;
#else
static const unsigned msan = 0;
#endif

#if ENABLE_TSAN
static const unsigned tsan = 1;
#else
static const unsigned tsan = 0;
#endif

#if ENABLE_UBSAN
static const unsigned ubsan = 1;
#else
static const unsigned ubsan = 0;
#endif

#if ENABLE_SANITIZER
static const unsigned sanitizer = 1;
#else
static const unsigned sanitizer = 0;
#endif

#if ENABLE_WORKSPACE_EMULATOR
static const unsigned workspace_emulator = 1;
#else
static const unsigned workspace_emulator = 0;
#endif

#if WITH_PERSISTENT_STORAGE
static const unsigned with_persistent_storage = 1;
#else
static const unsigned with_persistent_storage = 0;
#endif

static void v_matchproto_(cmd_f)
cmd_builtwith(CMD_ARGS)
{
	const char *feat;
	int good, skip, neg;

	(void)priv;

	if (av == NULL)
		return;

#define FEATURE(nm, tst)				\
	do {						\
		if (!strcmp(feat, nm)) {		\
			good = 1;			\
			if (tst) {			\
				skip = neg;		\
			} else {			\
				skip = !neg;		\
			}				\
		}					\
	} while (0)

	skip = 0;

	for (av++; *av != NULL; av++) {
		good = 0;
		neg = 0;
		feat = *av;

		if (feat[0] == '!') {
			neg = 1;
			feat++;
		}

		FEATURE("64bit", sizeof(void*) == 8);
		FEATURE("persistent_storage", with_persistent_storage);
		FEATURE("coverage", coverage);
		FEATURE("asan", asan);
		FEATURE("msan", msan);
		FEATURE("tsan", tsan);
		FEATURE("ubsan", ubsan);
		FEATURE("sanitizer", sanitizer);
		FEATURE("workspace_emulator", workspace_emulator);

		if (!good)
			vtc_fatal(vl, "FAIL test, unknown feature: %s", feat);

		if (!skip)
			continue;

		vtc_stop = 2;
		if (neg)
			vtc_log(vl, 1,
			    "SKIPPING test, conflicting feature: %s", feat);
		else
			vtc_log(vl, 1,
			    "SKIPPING test, lacking feature: %s", feat);
		return;
	}
}

#define CMD_TOP(x) {#x, cmd_##x},

static struct cmds builtwith_cmds[3] = {
CMD_TOP(builtwith)
};

static __attribute__((constructor)) void
register_builtwith_top_cmds(void)
{
        register_top_cmds(builtwith_cmds, vcountof(builtwith_cmds));
#ifdef PACKAGE_VERSION
        extmacro_def("pkg_version", NULL, PACKAGE_VERSION);
#endif
#ifdef PACKAGE_BRANCH
        extmacro_def("pkg_branch", NULL, PACKAGE_BRANCH);
#endif
}

