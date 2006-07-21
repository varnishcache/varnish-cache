#!/usr/local/bin/tclsh8.4
#
# Generate various .c and .h files for the VCL compiler and the interfaces
# for it.

# Objects which operate on backends
set beobj {
	{ backend.host	HOSTNAME }
	{ backend.port	PORTNAME }
}

# Objects which operate on sessions

set spobj {
	{ req.request	STRING }
        { req.url	STRING }
        { obj.valid	BOOL }
        { obj.cacheable	BOOL }
        { obj.backend	BACKEND }
        { obj.ttl	TIME }
        { req.http.	HEADER }
}

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
set fp [open ../../include/vrt_obj.h]

puts $fo ""
puts $fo "const char *vrt_obj_h = "
while {[gets $fp a] >= 0} {
	puts $fo "\t\"$a\\n\""
}
puts $fo ";"

close $fo
close $fp
