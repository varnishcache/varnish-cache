#!/usr/local/bin/tclsh8.4
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2007 Linpro AS
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
  { backend.host	WO HOSTNAME }
  { backend.port	WO PORTNAME }
  { backend.dnsttl	WO TIME	 }
}

# Objects which operate on sessions

set spobj {
	{ client.ip
		RO IP
		{recv pipe pass hash miss hit fetch                }
	}
	{ server.ip
		RO IP
		{recv pipe pass hash miss hit fetch                }
	}
	{ req.request
		RO STRING
		{recv pipe pass hash miss hit fetch                }
	}
	{ req.url
		RO STRING
		{recv pipe pass hash miss hit fetch                }
	}
	{ req.proto
		RO STRING
		{recv pipe pass hash miss hit fetch                }
	}
	{ req.backend
		RW BACKEND
		{recv pipe pass hash miss hit fetch                }
	}
	{ req.http.
		RO HEADER
		{recv pipe pass hash miss hit fetch                }
	}
	{ req.hash
		WO HASH
		{               hash                               }
	}
	{ obj.valid
		RW BOOL
		{                         hit fetch discard timeout}
	}
	{ obj.cacheable
		RW BOOL
		{                         hit fetch discard timeout}
	}
	{ obj.ttl
		RW TIME
		{                         hit fetch discard timeout}
	}
	{ resp.proto
		RO STRING
		{                             fetch                }
	}
	{ resp.status
		RO INT
		{                             fetch                }
	}
	{ resp.response
		RO STRING
		{                             fetch                }
	}
	{ resp.http.
		RO HEADER
		{                             fetch                }
	}
}

set tt(IP)	"struct sockaddr *"
set tt(STRING)	"const char *"
set tt(BOOL)	"unsigned"
set tt(BACKEND)	"struct backend *"
set tt(TIME)	"double"
set tt(INT)	"int"
set tt(HEADER)	"const char *"
set tt(HOSTNAME) "const char *"
set tt(PORTNAME) "const char *"
set tt(HASH) 	"const char *"

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

proc method_map {m} {

	set l ""
	foreach i $m {
		append l " | "
		append l VCL_MET_[string toupper $i]
	}
	return [string range $l 3 end]
}

proc vars {v ty pa} {
	global tt fo fp

	foreach v $v {
		set n [lindex $v 0]
		regsub -all {[.]} $n "_" m
		set a [lindex $v 1]
		set t [lindex $v 2]
		puts $fo  "\t\{ \"$n\", $t, [string length $n],"
		if {$a == "RO" || $a == "RW"} {
			puts $fo  "\t    \"VRT_r_${m}($pa)\","
			puts $fp  "$tt($t) VRT_r_${m}($ty);"
		} else {
			puts $fo  "\t    NULL,"
		}
		if {$a == "WO" || $a == "RW"} {
			puts $fo  "\t    \"VRT_l_${m}($pa, \","
			puts $fp  "void VRT_l_${m}($ty, $tt($t));"
		} else {
			puts $fo  "\t    NULL,"
		}
		puts $fo  "\t    V_$a,"
		puts $fo  "\t    [method_map [lindex $v 3]]"
		puts $fo "\t\},"

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
