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

# Objects available in backends
set beobj {
  { backend.host	WO HOSTNAME }
  { backend.port	WO PORTNAME }
  { backend.dnsttl	WO TIME	 }
}

# Variables available in sessions
# Comments are stripped from #...\n
set spobj {

	# Connection related parameters
	{ client.ip
		RO IP
		{recv pipe pass hash miss hit fetch                        }
	}
	{ client.bandwidth				 # Not implemented yet
		NO
	}
	{ server.ip
		RO IP
		{recv pipe pass hash miss hit fetch                        }
	}

	# Request paramters
	{ req.request
		RO STRING
		{recv pipe pass hash miss hit fetch                        }
	}
	{ req.url
		RO STRING
		{recv pipe pass hash miss hit fetch                        }
	}
	{ req.proto
		RO STRING
		{recv pipe pass hash miss hit fetch                        }
	}
	{ req.http.
		RW HEADER
		{recv pipe pass hash miss hit fetch                        }
	}

	# Possibly misnamed, not really part of the request
	{ req.hash
		WO HASH
		{               hash                                       }
	}
	{ req.backend
		RW BACKEND
		{recv pipe pass hash miss hit fetch                        }
	}

	# Request sent to backend
	{ bereq.request
		RW STRING
		{     pipe pass      miss                                  }
	}
	{ bereq.url
		RW STRING
		{     pipe pass      miss                                  }
	}
	{ bereq.proto
		RW STRING
		{     pipe pass      miss                                  }
	}
	{ bereq.http.
		RW HEADER
		{     pipe pass      miss                                  }
	}

	# The (possibly) cached object
	{ obj.proto
		RW STRING
		{                         hit fetch deliver                }
	}
	{ obj.status
		RW INT
		{                             fetch                        }
	}
	{ obj.response
		RW STRING
		{                             fetch                        }
	}
	{ obj.http.
		RW HEADER
		{                         hit fetch deliver                }
	}

	{ obj.valid
		RW BOOL
		{                         hit fetch         discard timeout}
	}
	{ obj.cacheable
		RW BOOL
		{                         hit fetch         discard timeout}
	}
	{ obj.ttl
		RW TIME
		{                         hit fetch         discard timeout}
	}
	{ obj.lastuse
		RO TIME
		{                         hit fetch deliver discard timeout}
	}

	# The response we send back
	{ resp.proto
		RW STRING
		{                             fetch                        }
	}
	{ resp.status
		RW INT
		{                             fetch                        }
	}
	{ resp.response
		RW STRING
		{                             fetch                        }
	}
	{ resp.http.
		RW HEADER
		{                             fetch                        }
	}

	# Miscellaneous
	{ now
		RO TIME
		{recv pipe pass hash miss hit fetch deliver discard timeout}
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

	regsub -all "#\[^\n\]*\n" $v "" v
	foreach v $v {
		set n [lindex $v 0]
		regsub -all {[.]} $n "_" m
		set a [lindex $v 1]
		if {$a == "NO"} continue
		set t [lindex $v 2]
		puts $fo  "\t\{ \"$n\", $t, [string length $n],"
		if {$a == "RO" || $a == "RW"} {
			puts $fo  "\t    \"VRT_r_${m}($pa)\","
			if {$t != "HEADER"} {
				puts $fp  "$tt($t) VRT_r_${m}($ty);"
			}
		} else {
			puts $fo  "\t    NULL,"
		}
		if {$a == "WO" || $a == "RW"} {
			puts $fo  "\t    \"VRT_l_${m}($pa, \","
			if {$t != "HEADER"} {
				puts $fp  "void VRT_l_${m}($ty, $tt($t));"
			}
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
