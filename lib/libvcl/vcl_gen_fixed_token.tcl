#!/usr/local/bin/tclsh8.4
#
# Generate a C source file to recognize a set of tokens for the
# Varnish 

set keywords {
	if else elseif elsif

	func proc sub

	acl

	backend

	error
	pass
	fetch
	call
	no_cache
	no_new_cache
	set
	rewrite
	finish
	switch_config
}

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
	{"/="	DECR}
	{"*="	MUL}
	{"/="	DIV}
}

set char {{}()*+-/%><=;!&.|~,}

set extras {ID VAR CNUM CSTR EOI}

set fo [open "vcl_fixed_token.c" w]

puts $fo {/*
 * NB:  This file is machine generated, DO NOT EDIT!
 * instead, edit the Tcl script vcl_gen_fixed_token.tcl and run it by hand
 */
}

set foh [open "vcl_token_defs.h" w]
puts $foh {/*
 * NB:  This file is machine generated, DO NOT EDIT!
 * instead, edit the Tcl script vcl_gen_fixed_token.tcl and run it by hand
 */
}

puts $fo "#include <stdio.h>"
puts $fo "#include <ctype.h>"
puts $fo "#include \"vcl_priv.h\""

set tn 128
puts $foh "#define LOW_TOKEN $tn"

foreach k $keywords {
	set t T_[string toupper $k]
	lappend tokens [list $t $k]
	puts $foh "#define $t $tn"
	incr tn
	lappend fixed [list $k $t 1]
}
foreach k $magic {
	set t T_[string toupper [lindex $k 1]]
	lappend tokens [list $t [lindex $k 0]]
	puts $foh "#define $t $tn"
	incr tn
	lappend fixed [list [lindex $k 0] $t 0]
}
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

set ll 0

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
	if {$cx != $ll} {
		if {$ll} {
			puts $fo "		return (0);"
		}
	
		puts $fo "	case '$ch':"
		set ll $cx
	}
	foreach tt $l {
		set k [lindex $tt 0]
		puts -nonewline $fo "		if ("
		for {set i 0} {$i < [string length $k]} {incr i} {
			if {$i > 0} {
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
		puts $fo "			*q = p + [string length $k];"
		puts $fo "			return ([lindex $tt 1]);"
		puts $fo "		}"
	}
} 
puts $fo "		return (0);"
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

set fi [open "../../include/vcl_lang.h"]

puts $fo ""
puts $fo "void"
puts $fo "vcl_output_lang_h(FILE *f)"
puts $fo "{"
while {[gets $fi a] >= 0} {
	if {"$a" == "#include <http_headers.h>"} {
		puts "FOO $a"
		set fx [open "../../include/http_headers.h"]
		while {[gets $fx b] >= 0} {
			regsub -all {"} $b {\"} b
			puts $fo "\tfputs(\"$b\\n\", f);"
		}
		close $fx
		continue
	}
	puts $fo "\tfputs(\"$a\\n\", f);"
}
puts $fo "}"
close $fi

close $foh
close $fo
