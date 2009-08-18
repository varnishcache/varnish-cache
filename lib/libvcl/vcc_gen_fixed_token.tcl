#!/usr/local/bin/tclsh8.4
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2009 Linpro AS
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

# These are the metods which can be called in the VCL program.
# Second element is list of valid return actions.
#
set methods {
	{recv		{error pass pipe lookup}}
	{pipe		{error pipe}}
	{pass		{error restart pass}}
	{hash		{hash}}
	{miss		{error restart pass fetch}}
	{hit		{error restart pass deliver}}
	{fetch		{error restart pass deliver}}
	{deliver	{restart deliver}}
	{error		{restart deliver}}
}

# Language keywords
#
set keywords {
	include

	if else elseif elsif
}

# Non-word tokens
#
set magic {
	{"++"	INC}
	{"--"	DEC}
	{"&&"	CAND}
	{"||"	COR}
	{"<="	LEQ}
	{"=="	EQ}
	{"!="	NEQ}
	{">="	GEQ}
	{">>"	SHR}
	{"<<"	SHL}
	{"+="	INCR}
	{"-="	DECR}
	{"*="	MUL}
	{"/="	DIV}
	{"!~"	NOMATCH}
}

# Single char tokens
#
set char {{}()*+-/%><=;!&.|~,}

# Other token identifiers
#
set extras {ID VAR CNUM CSTR EOI CSRC}

#----------------------------------------------------------------------
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
	{recv pipe pass hash miss hit fetch deliver                error }
	"const struct sess *"
    }
    { req.url
	RW STRING
	{recv pipe pass hash miss hit fetch deliver                error }
	"const struct sess *"
    }
    { req.proto
	RW STRING
	{recv pipe pass hash miss hit fetch deliver                error }
	"const struct sess *"
    }
    { req.http.
	RW HDR_REQ
	{recv pipe pass hash miss hit fetch deliver                error }
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
	{recv pipe pass hash miss hit fetch deliver                error }
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

    { req.esi
	RW BOOL
	{recv fetch deliver					   error}
	"struct sess *"
    }

    #######################################################################
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

    #######################################################################
    # Response from the backend
    { beresp.proto
	RW STRING
	{                             fetch                        }
	"const struct sess *"
    }
    { beresp.status
	RW INT
	{                             fetch                        }
	"const struct sess *"
    }
    { beresp.response
	RW STRING
	{                             fetch                        }
	"const struct sess *"
    }
    { beresp.http.
	RW HDR_BERESP
	{                             fetch                        }
	"const struct sess *"
    }
    { beresp.cacheable
	RW BOOL
	{                             fetch                              }
	"const struct sess *"
    }
    { beresp.ttl
	RW TIME
	{                             fetch				 }
	"const struct sess *"
    }
    { beresp.grace
	RW TIME
	{                             fetch				 }
	"const struct sess *"
    }

    #######################################################################
    # The (possibly) cached object
    { obj.proto
	RW STRING
	{                         hit                               error}
	"const struct sess *"
    }
    { obj.status
	RW INT
	{                                                           error}
	"const struct sess *"
    }
    { obj.response
	RW STRING
	{                                                           error}
	"const struct sess *"
    }
    { obj.hits
	RO INT
	{			  hit       deliver                      }
	"const struct sess *"
    }
    { obj.http.
	RW HDR_OBJ
	{                         hit       			    error}
	"const struct sess *"
    }

    { obj.cacheable
	RW BOOL
	{                         hit                                    }
	"const struct sess *"
    }
    { obj.ttl
	RW TIME
	{                         hit               error}
	"const struct sess *"
    }
    { obj.grace
	RW TIME
	{                         hit               error}
	"const struct sess *"
    }
    { obj.lastuse
	RO TIME
	{                         hit       deliver error}
	"const struct sess *"
    }
    { obj.hash
	RO STRING
	{                    miss hit       deliver                 error}
	"const struct sess *"
    }

    #######################################################################
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
	    {recv pipe pass hash miss hit fetch deliver }
	    "const struct sess *"
    }
    { req.backend.healthy	RO BOOL
	    {recv pipe pass hash miss hit fetch deliver }
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
# Figure out the union list of return actions
foreach i $methods {
	foreach j [lindex $i 1] {
		set tmp($j) 1
	}
}
set returns [lsort [array names tmp]]
unset tmp

#----------------------------------------------------------------------
# Boilerplate warning for all generated files.

proc warns {fd} {

	puts $fd "/*"
	puts $fd \
	    { * $Id$}
	puts $fd " *"
	puts $fd " * NB:  This file is machine generated, DO NOT EDIT!"
	puts $fd " *"
	puts $fd " * Edit and run vcc_gen_fixed_token.tcl instead"
	puts $fd " */"
	puts $fd ""
}

#----------------------------------------------------------------------
# Include a .h file as a string.

proc copy_include {n} {
	global fo

	puts $fo "\n\t/* $n */\n"
	set fi [open $n]
	set n 0
	while {[gets $fi a] >= 0} {
		for {set b 0} {$b < [string length $a]} {incr b} {
			if {$n == 0} {
				puts -nonewline $fo "\tvsb_cat(sb, \""
			}
			set c [string index $a $b]
			if {"$c" == "\\"} {
				puts -nonewline $fo "\\\\"
				incr n
			} elseif {"$c" == "\t"} {
				puts -nonewline $fo "\\t"
				incr n
			} else {
				puts -nonewline $fo "$c"
			}
			incr n
			if {$n > 53} {
				puts $fo "\");"
				set n 0
			}
		}
		if {$n == 0} {
			puts -nonewline $fo "\tvsb_cat(sb, \""
		}
		puts -nonewline $fo "\\n"
		incr n 2
		if {$n > 30} {
			puts $fo "\");"
			set n 0
		}
	}
	if {$n > 0} {
		puts $fo "\");"
	}
	close $fi
}

#----------------------------------------------------------------------
# Build the variable related .c and .h files

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


#----------------------------------------------------------------------
# Build the vcl.h #include file

set fo [open ../../include/vcl.h w]
warns $fo
puts $fo {struct sess;
struct cli;

typedef void vcl_init_f(struct cli *);
typedef void vcl_fini_f(struct cli *);
typedef int vcl_func_f(struct sess *sp);
}

puts $fo "/* VCL Methods */"
set u 0
foreach m $methods {
	if {[string length [lindex $m 0]] < 8} {
		set sp "\t"
	} else {
		set sp ""
	}
	puts $fo "#define VCL_MET_[string toupper [lindex $m 0]]\t${sp}(1 << $u)"
	incr u
}

puts $fo "\n#define VCL_MET_MAX\t\t$u\n"

puts $fo "/* VCL Returns */"
set i 0
foreach k $returns {
	puts $fo "#define VCL_RET_[string toupper $k]\t\t$i"
	incr i
}
puts $fo "\n#define VCL_RET_MAX\t\t$i\n"

puts $fo "struct VCL_conf {"
puts $fo {	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */

	struct director	**director;
	unsigned	ndirector;
	struct vrt_ref	*ref;
	unsigned	nref;
	unsigned	busy;
	unsigned	discard;

	unsigned	nsrc;
	const char	**srcname;
	const char	**srcbody;

	unsigned	nhashcount;

	vcl_init_f	*init_func;
	vcl_fini_f	*fini_func;
}
foreach m $methods {
	puts $fo "\tvcl_func_f\t*[lindex $m 0]_func;"
}
puts $fo "};"

close $fo

#----------------------------------------------------------------------
# Build the vcl_returns.h #include file

set for [open "../../include/vcl_returns.h" w]
warns $for
puts $for "#ifdef VCL_RET_MAC"
set i 0
foreach k $returns {
	set u [string toupper $k]
	if {$k == "error"} {
		puts $for "VCL_RET_MAC($k, $u)"
	} else {
		puts $for "VCL_RET_MAC($k, $u)"
	}
	incr i
}
puts $for "#endif"
puts $for ""
puts $for "#ifdef VCL_MET_MAC"
set u 0
foreach m $methods {
	puts -nonewline $for "VCL_MET_MAC([lindex $m 0]"
	puts -nonewline $for ",[string toupper [lindex $m 0]]"
	set l [lindex $m 1]
	puts $for ","
	puts $for "     ((1 << VCL_RET_[string toupper [lindex $l 0]])"
	foreach r [lrange $l 1 end] {
		puts $for "    | (1 << VCL_RET_[string toupper $r])"
	}
	puts $for "))"
	incr u
}
puts $for "#endif"
close $for

#----------------------------------------------------------------------
# Build the compiler token table and recognizers

set fo [open "vcc_fixed_token.c" w]
warns $fo

set foh [open "vcc_token_defs.h" w]
warns $foh

puts $fo "#include \"config.h\""
puts $fo "#include <stdio.h>"
puts $fo "#include <ctype.h>"
puts $fo "#include \"config.h\""
puts $fo "#include \"vcc_priv.h\""
puts $fo "#include \"vsb.h\""

set tn 128
puts $foh "#define LOW_TOKEN $tn"


proc add_token {tok str alpha} {
	global tokens tn fixed foh

	lappend tokens [list $tok $str]
	puts $foh "#define $tok $tn"
	incr tn
	lappend fixed [list $str $tok $alpha]
}

proc mk_token {tok str alpha} {
	set tok T_[string toupper $tok]
	add_token $tok $str $alpha
}

foreach k $keywords { mk_token $k $k 1 }
foreach k $magic { mk_token [lindex $k 1] [lindex $k 0] 0 }
foreach k $extras {
	set t [string toupper $k]
	lappend tokens [list $t $t]
	puts $foh "#define [string toupper $k] $tn"
	incr tn
}
for {set i 0} {$i < [string length $char]} {incr i} {
	set t [string index $char $i]
	lappend token2 [list '$t' T$t]
	lappend fixed [list "$t" '$t' 0]
}

set tokens [lsort $tokens]
set token2 [lsort $token2]

# We want to output in ascii order: create sorted first char list
foreach t $fixed {
	set xx([string index [lindex $t 0] 0]) 1
}
set seq [lsort [array names xx]]

puts $fo {
#define M1()     do {*q = p + 1; return (p[0]); } while (0)
#define M2(c, t) do {if (p[1] == (c)) { *q = p + 2; return (t); }} while (0)

unsigned
vcl_fixed_token(const char *p, const char **q)}
puts $fo "{"
puts $fo ""
puts $fo "	switch (p\[0\]) {"

foreach ch "$seq" {
	# Now find all tokens starting with ch
	set l ""
	foreach t $fixed {
		if {[string index [lindex $t 0] 0] == $ch} {
			lappend l $t
		}
	}
	# And do then in reverse order to match longest first
	set l [lsort -index 0 -decreasing $l]
	scan "$ch" "%c" cx
	puts $fo "	case '$ch':"
	set retval "0"
	set m1 0
	foreach ty $l {
		set k [lindex $ty 0]
		if {[string length $k] == 1} {
			puts $fo "\t\tM1();"
			set m1 1
			continue;
		}
		if {[string length $k] == 2} {
			puts -nonewline $fo "		M2("
			puts -nonewline $fo "'[string index $k 1]'"
			puts            $fo ", [lindex $ty 1]);"
			continue;
		} 
		puts -nonewline $fo "		if ("
		for {set i 1} {$i < [string length $k]} {incr i} {
			if {$i > 1} {
				puts -nonewline $fo " &&"
				if {[expr $i % 3] == 1} {
					puts -nonewline $fo "\n\t\t   "
				}
				puts -nonewline $fo " "
			}
			puts -nonewline $fo "p\[$i\] == '[string index $k $i]'"
		}
		if {[lindex $ty 2]} {
			if {[expr $i % 3] == 1} {
				puts -nonewline $fo "\n\t\t    "
			}
			puts -nonewline $fo " && !isvar(p\[$i\])"
		}
		puts $fo ") {"
		puts $fo "\t\t\t*q = p + [string length $k];"
		puts $fo "\t\t\treturn ([lindex $ty 1]);"
		puts $fo "\t\t}"
	}
	if {$m1 == 0} {
		puts $fo "\t\treturn ($retval);"
	}
}

puts $fo "	default:"
puts $fo "		return (0);"
puts $fo "	}"
puts $fo "}"

puts $fo ""
puts $fo "const char * const vcl_tnames\[256\] = {"
foreach i $token2 {
	puts $fo "\t\[[lindex $i 0]\] = \"[lindex $i 0]\","
}
foreach i $tokens {
	puts $fo "\t\[[lindex $i 0]\] = \"[lindex $i 1]\","
}
puts $fo "};"

puts $fo ""
puts $fo "void"
puts $fo "vcl_output_lang_h(struct vsb *sb)"
puts $fo "{"

copy_include ../../include/vcl.h
copy_include ../../include/vrt.h
copy_include ../../include/vrt_obj.h

puts $fo "}"

close $foh
close $fo

