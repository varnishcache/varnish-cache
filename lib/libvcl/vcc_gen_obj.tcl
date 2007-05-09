#!/usr/local/bin/tclsh8.4
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006 Linpro AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# Generate various .c and .h files for the VCL compiler and the interfaces
# for it.

# Objects which operate on backends
set beobj {
	{ backend.host		HOSTNAME }
	{ backend.port		PORTNAME }
	{ backend.dnsttl	TIME }
}

# Objects which operate on sessions

set spobj {
	{ client.ip		IP }
	{ server.ip		IP }
	{ req.request		STRING }
	{ req.host		STRING }
        { req.url		STRING }
        { req.proto		STRING }
        { req.backend		BACKEND }
        { obj.valid		BOOL }
        { obj.cacheable		BOOL }
        { obj.ttl		TIME }
        { req.http.		HEADER }
        { resp.http.		HEADER }
}

set tt(IP)	"struct sockaddr *"
set tt(STRING)	"const char *"
set tt(BOOL)	"double"
set tt(BACKEND)	"struct backend *"
set tt(TIME)	"double"
set tt(HEADER)	"const char *"
set tt(HOSTNAME) "const char *"
set tt(PORTNAME) "const char *"

#----------------------------------------------------------------------
# Boilerplate warning for all generated files.

proc warns {fd} {

	puts $fd "/*"
	puts $fd { * $Id$}
	puts $fd " *"
	puts $fd " * NB:  This file is machine generated, DO NOT EDIT!"
	puts $fd " *"
	puts $fd " * Edit vcc_gen_obj.tcl instead"
	puts $fd " */"
	puts $fd ""
}

set fo [open vcc_obj.c w]
warns $fo
set fp [open ../../include/vrt_obj.h w]
warns $fp

proc vars {v ty pa} {
	global tt fo fp

	foreach v $v {
		set n [lindex $v 0]
		regsub -all {[.]} $n "_" m
		set t [lindex $v 1]
		puts $fo  "\t\{ \"$n\", $t, [string length $n],"
		puts $fo  "\t    \"VRT_r_${m}($pa)\","
		puts $fo  "\t    \"VRT_l_${m}($pa, \","
		puts $fo "\t\},"

		puts $fp  "$tt($t) VRT_r_${m}($ty);"
		puts $fp  "void VRT_l_${m}($ty, $tt($t));"
	}
	puts $fo "\t{ NULL }"
}

puts $fo "#include <stdio.h>"
puts $fo "#include \"vcc_compile.h\""
puts $fo ""

puts $fo "struct var vcc_be_vars\[\] = {"
vars $beobj "struct backend *" "backend"
puts $fo "};"

puts $fo ""

puts $fo "struct var vcc_vars\[\] = {"
vars $spobj "struct sess *" "sp"
puts $fo "};"

close $fp
