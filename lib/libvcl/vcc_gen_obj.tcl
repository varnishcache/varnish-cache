#!/usr/local/bin/tclsh8.4
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2008 Linpro AS
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


# Variables available in sessions
# Comments are stripped from #...\n
set spobj {

    # Connection related parameters
    { client.ip
	RO IP
	{recv pipe pass hash miss hit fetch deliver                error }
	"const struct sess *"
    }
    { client.bandwidth				 # Not implemented yet
	NO
	{ }
	"const struct sess *"
    }
    { server.ip
	RO IP
	{recv pipe pass hash miss hit fetch deliver                error }
	"struct sess *"
    }
    { server.hostname
	RO STRING
	{recv pipe pass hash miss hit fetch deliver                error }
	"struct sess *"
    }
    { server.identity
	RO STRING
	{recv pipe pass hash miss hit fetch deliver                error }
	"struct sess *"
    }
    { server.port
	RO INT
	{recv pipe pass hash miss hit fetch deliver                error }
	"struct sess *"
    }
    # Request paramters
    { req.request
	RW STRING
	{recv pipe pass hash miss hit fetch                        error }
	"const struct sess *"
    }
    { req.url
	RW STRING
	{recv pipe pass hash miss hit fetch                        error }
	"const struct sess *"
    }
    { req.proto
	RW STRING
	{recv pipe pass hash miss hit fetch                        error }
	"const struct sess *"
    }
    { req.http.
	RW HDR_REQ
	{recv pipe pass hash miss hit fetch                        error }
	"const struct sess *"
    }

    # Possibly misnamed, not really part of the request
    { req.hash
	WO HASH
	{               hash                                       error }
	"struct sess *"
    }
    { req.backend
	RW BACKEND
	{recv pipe pass hash miss hit fetch                        error }
	"struct sess *"
    }
    { req.restarts
	RO INT
	{recv pipe pass hash miss hit fetch deliver                error }
	"const struct sess *"
    }
    { req.grace
	RW TIME
	{recv pipe pass hash miss hit fetch deliver		   error }
	"struct sess *"
    }

    { req.xid
	RO STRING
	{recv pipe pass hash miss hit fetch deliver		   error}
	"struct sess *"
    }

    # Request sent to backend
    { bereq.request
	RW STRING
	{     pipe pass      miss     fetch                        }
	"const struct sess *"
    }
    { bereq.url
	RW STRING
	{     pipe pass      miss     fetch                        }
	"const struct sess *"
    }
    { bereq.proto
	RW STRING
	{     pipe pass      miss     fetch                        }
	"const struct sess *"
    }
    { bereq.http.
	RW HDR_BEREQ
	{     pipe pass      miss     fetch                        }
	"const struct sess *"
    }
    { bereq.connect_timeout
	RW TIME
	{     pass      miss     }
	"struct sess *"
    }
    { bereq.first_byte_timeout
	RW TIME
	{     pass      miss     }
	"struct sess *"
    }
    { bereq.between_bytes_timeout
	RW TIME
	{     pass      miss     }
	"struct sess *"
    }

    # The (possibly) cached object
    { obj.proto
	RW STRING
	{                         hit fetch                         error}
	"const struct sess *"
    }
    { obj.status
	RW INT
	{                             fetch                         error}
	"const struct sess *"
    }
    { obj.response
	RW STRING
	{                             fetch                         error}
	"const struct sess *"
    }
    { obj.hits
	RO INT
	{			  hit fetch deliver                      }
	"const struct sess *"
    }
    { obj.http.
	RW HDR_OBJ
	{                         hit fetch 			    error}
	"const struct sess *"
    }

    { obj.cacheable
	RW BOOL
	{                         hit fetch         discard timeout error}
	"const struct sess *"
    }
    { obj.ttl
	RW TIME
	{                         hit fetch         discard timeout error}
	"const struct sess *"
    }
    { obj.grace
	RW TIME
	{                         hit fetch         discard timeout error}
	"const struct sess *"
    }
    { obj.prefetch
	RW RTIME
	{ fetch prefetch }
	"const struct sess *"
    }
    { obj.lastuse
	RO TIME
	{                         hit fetch deliver discard timeout error}
	"const struct sess *"
    }
    { obj.hash
	RO STRING
	{                    miss hit fetch deliver                 error}
	"const struct sess *"
    }

    # The response we send back
    { resp.proto
	RW STRING
	{                                   deliver                }
	"const struct sess *"
    }
    { resp.status
	RW INT
	{                                   deliver                }
	"const struct sess *"
    }
    { resp.response
	RW STRING
	{                                   deliver                }
	"const struct sess *"
    }
    { resp.http.
	RW HDR_RESP
	{                                   deliver                }
	"const struct sess *"
    }

    # Miscellaneous
    # XXX: I'm not happy about this one.  All times should be relative
    # XXX: or delta times in VCL programs, so this shouldn't be needed /phk
    { now
	    RO TIME
	    {recv pipe pass hash miss hit fetch deliver discard timeout}
	    "const struct sess *"
    }
    { req.backend.healthy	RO BOOL
	    {recv pipe pass hash miss hit fetch deliver discard timeout}
	    "const struct sess *"
    }

}

set tt(IP)		"struct sockaddr *"
set tt(STRING)		"const char *"
set tt(BOOL)		"unsigned"
set tt(BACKEND)		"struct director *"
set tt(TIME)		"double"
set tt(RTIME)		"double"
set tt(INT)		"int"
set tt(HDR_RESP)	"const char *"
set tt(HDR_OBJ)		"const char *"
set tt(HDR_REQ)		"const char *"
set tt(HDR_BEREQ)	"const char *"
set tt(HOSTNAME) 	"const char *"
set tt(PORTNAME) 	"const char *"
set tt(HASH) 		"const char *"
set tt(SET) 		"struct vrt_backend_entry *"

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

	set l1 ""
	set l2 ""
	foreach i $m {
		if {[string length $l2] > 55} {
			if {$l1 != ""} {
				append l1 "\n\t    "
			}
			append l1 "$l2"
			set l2 ""
		}
		if {$l2 != "" || $l1 != ""} {
			append l2 " | "
		}
		append l2 VCL_MET_[string toupper $i]
	}
	if {$l2 != ""} {
		if {$l1 != ""} {
			append l1 "\n\t    "
		}
		append l1 "$l2"
	}
	if {$l1 == ""} {
		return "0"
	}
	return $l1
}

proc vars {v pa} {
	global tt fo fp

	regsub -all "#\[^\n\]*\n" $v "" v
	foreach v $v {
		set n [lindex $v 0]
		regsub -all {[.]} $n "_" m
		set a [lindex $v 1]
		if {$a == "NO"} continue
		set t [lindex $v 2]
		set ty [lindex $v 4]
		if {[regexp HDR_ $t]} {
			puts $fo  "\t\{ \"$n\", HEADER, [string length $n],"
		} else {
			puts $fo  "\t\{ \"$n\", $t, [string length $n],"
		}
		if {$a == "RO" || $a == "RW"} {
			puts -nonewline $fo  "\t    \"VRT_r_${m}($pa)\","
			if {![regexp HDR_ $t]} {
				puts $fp  "$tt($t) VRT_r_${m}($ty);"
			}
		} else {
			puts -nonewline $fo  "\t    NULL,"
		}
		if {$a == "WO" || $a == "RW"} {
			puts $fo  "\t    \"VRT_l_${m}($pa, \","
			if {[regexp HDR_ $t]} {
			} elseif {$t == "STRING"} {
				puts $fp  "void VRT_l_${m}($ty, $tt($t), ...);"
			} else {
				puts $fp  "void VRT_l_${m}($ty, $tt($t));"
			}
		} else {
			puts $fo  "\t    NULL,"
		}
		puts -nonewline $fo  "\t    V_$a,"
		if {![regexp HDR_ $t]} {
			puts $fo  "\t    0,"
		} else {
			puts $fo  "\t    \"$t\","
		}
		puts $fo  "\t    [method_map [lindex $v 3]]"
		puts $fo "\t\},"

	}
	puts $fo "\t{ NULL }"
}

puts $fo "#include \"config.h\""
puts $fo "#include <stdio.h>"
puts $fo "#include \"vcc_compile.h\""
puts $fo ""

puts $fo "struct var vcc_vars\[\] = {"
vars $spobj "sp"
puts $fo "};"

close $fp
