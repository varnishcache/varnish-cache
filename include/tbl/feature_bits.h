/*-
 * Copyright (c) 2012 Varnish Software AS
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
 * Fields in the feature parameter
 *
 */

FEATURE_BIT(SHORT_PANIC,		short_panic,
    "Short panic message.",
    "Reduce level of detail for panic messages."
)
FEATURE_BIT(WAIT_SILO,			wait_silo,
    "Wait for persistent silo.",
    "Wait for persistent silos to load completely before serving requests."
)
FEATURE_BIT(NO_COREDUMP,		no_coredump,
    "No coredumps.",
    "Don't attempt to coredump child process on panics."
)
FEATURE_BIT(ESI_IGNORE_HTTPS,		esi_ignore_https,
    "Treat HTTPS as HTTP in ESI:includes",
    "Convert <esi:include src\"https://... to http://..."
)
FEATURE_BIT(ESI_DISABLE_XML_CHECK,	esi_disable_xml_check,
    "Don't check of body looks like XML",
    "Allow ESI processing on any kind of object"
)
FEATURE_BIT(ESI_IGNORE_OTHER_ELEMENTS,	esi_ignore_other_elements,
    "Ignore non-esi XML-elements",
    "Allows syntax errors in the XML"
)
FEATURE_BIT(ESI_REMOVE_BOM,		esi_remove_bom,
    "Remove UTF-8 BOM",
    "Remove UTF-8 BOM from front of object."
    "Ignore and remove the UTF-8 BOM (0xeb 0xbb 0xbf) from front of object."
)
