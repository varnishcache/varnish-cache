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

# These are the metods which can be called in the VCL program. 
# Second element is list of valid return actions.
#
set methods {
	{recv		{error restart pass pipe lookup}}
	{pipe		{error restart pipe}}
	{pass		{error restart pass}}
	{hash		{hash}}
	{miss		{error restart pass fetch}}
	{hit		{error restart pass deliver}}
	{fetch		{error restart pass insert}}
	{deliver	{error restart deliver}}
	{prefetch	{fetch pass}}
	{timeout	{fetch discard}}
	{discard	{discard keep}}
}

# These are the return actions
#
set returns {
	error
	lookup
	hash
	pipe
	pass
	fetch
	insert
	deliver
	discard
	keep
	restart
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
}

# Single char tokens
#
set char {{}()*+-/%><=;!&.|~,}

# Other token identifiers
#
set extras {ID VAR CNUM CSTR EOI CSRC}

#----------------------------------------------------------------------
# Boilerplate warning for all generated files.

proc warns {fd} {

	puts $fd "/*"
	puts $fd { * $Id$}
	puts $fd " *"
	puts $fd " * NB:  This file is machine generated, DO NOT EDIT!"
	puts $fd " *"
	puts $fd " * Edit vcc_gen_fixed_token.tcl instead"
	puts $fd " */"
	puts $fd ""
}

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
puts $fo "struct VCL_conf {"
puts $fo {	unsigned        magic;
#define VCL_CONF_MAGIC  0x7406c509      /* from /dev/random */

        struct director  **director;
        unsigned        ndirector;
        struct vrt_ref  *ref;
        unsigned        nref;
        unsigned        busy;
        unsigned        discard;
        
	unsigned	nsrc;
	const char	**srcname;
	const char	**srcbody;

	unsigned	nhashcount;

        void            *priv;

        vcl_init_f      *init_func;
        vcl_fini_f      *fini_func;
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
	if {$k == "error"} {
		puts $for "#ifdef VCL_RET_MAC_E"
		puts $for "VCL_RET_MAC_E($k, [string toupper $k], (1 << $i), $i)"
		puts $for "#endif"
	} else {
		puts $for "VCL_RET_MAC($k, [string toupper $k], (1 << $i), $i)"
	}
	incr i
}
puts $for "#else"
set i 0
foreach k $returns {
	puts $for "#define VCL_RET_[string toupper $k]  (1 << $i)"
	incr i
}
puts $for "#define VCL_RET_MAX $i"
puts $for "#endif"
puts $for ""
puts $for "#ifdef VCL_MET_MAC"
set u 0
foreach m $methods {
	puts -nonewline $for "VCL_MET_MAC([lindex $m 0]"
	puts -nonewline $for ",[string toupper [lindex $m 0]]"
	set l [lindex $m 1]
	puts -nonewline $for ",(VCL_RET_[string toupper [lindex $l 0]]"
	foreach r [lrange $l 1 end] {
		puts -nonewline $for "|VCL_RET_[string toupper $r]"
	}
	puts -nonewline $for ")"
	puts $for ")"
	incr u
}
puts $for "#else"
set u 0
foreach m $methods {
	puts $for "#define VCL_MET_[string toupper [lindex $m 0]]\t(1 << $u)"
	incr u
}
puts $for "#endif"
puts $for "#define N_METHODS $u"
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
	foreach tt $l {
		set k [lindex $tt 0]
		if {[string length $k] == 1} {
			puts $fo "\t\t*q = p + 1;"
			set retval {p[0]}
			continue;
		}
		puts -nonewline $fo "		if ("
		for {set i 1} {$i < [string length $k]} {incr i} {
			if {$i > 1} {
				puts -nonewline $fo " && "
				if {![expr $i % 3]} {
					puts -nonewline $fo "\n\t\t    "
				}
			}
			puts -nonewline $fo "p\[$i\] == '[string index $k $i]'"
		}
		if {[lindex $tt 2]} {
			if {![expr $i % 3]} {
				puts -nonewline $fo "\n\t\t    "
			}
			puts -nonewline $fo " && !isvar(p\[$i\])"
		}
		puts $fo ") {"
		puts $fo "\t\t\t*q = p + [string length $k];"
		puts $fo "\t\t\treturn ([lindex $tt 1]);"
		puts $fo "\t\t}"
	}
	puts $fo "\t\treturn ($retval);"
} 

puts $fo "	default:"
puts $fo "		return (0);"
puts $fo "	}"
puts $fo "}"

puts $fo ""
puts $fo "const char *vcl_tnames\[256\];\n"
puts $fo "void"
puts $fo "vcl_init_tnames(void)"
puts $fo "{"
foreach i $token2 {
	puts $fo "\tvcl_tnames\[[lindex $i 0]\] = \"[lindex $i 0]\";"
}
foreach i $tokens {
	puts $fo "\tvcl_tnames\[[lindex $i 0]\] = \"[lindex $i 1]\";"
}
puts $fo "}"

#----------------------------------------------------------------------
# Create the C-code which emits the boilerplate definitions for the
# generated C code output

proc copy_include {n} {
	global fo

	set fi [open $n]
	while {[gets $fi a] >= 0} {
		regsub -all {\\} $a {\\\\} a
		puts $fo "\tvsb_cat(sb, \"$a\\n\");"
	}
	close $fi
}

puts $fo ""
puts $fo "void"
puts $fo "vcl_output_lang_h(struct vsb *sb)"
puts $fo "{"
set i 0
foreach k $returns {
	puts $fo "\tvsb_cat(sb, \"#define VCL_RET_[string toupper $k]  (1 << $i)\\n\");"
	incr i
}

copy_include ../../include/vcl.h
copy_include ../../include/vrt.h
copy_include ../../include/vrt_obj.h

puts $fo "}"

close $foh
close $fo
