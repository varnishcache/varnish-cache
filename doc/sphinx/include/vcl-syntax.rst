VCL SYNTAX
==========

The VCL syntax is very simple, and deliberately similar to C and Perl.
Blocks are delimited by curly braces, statements end with semicolons,
and comments may be written as in C, C++ or Perl according to your own
preferences.

In addition to the C-like assignment (=), comparison (==, !=) and
boolean (!, && and \|\|) operators, VCL supports both regular
expression and ACL matching using the ~ and the !~ operators.

Basic strings are enclosed in " ... ", and may not contain newlines.

Long strings are enclosed in {" ... "}. They may contain any
character including ", newline and other control characters except
for the NUL (0x00) character.

Unlike C and Perl, the backslash (\) character has no special meaning
in strings in VCL, so it can be freely used in regular expressions
without doubling.

Strings are concatenated using the '+' operator. 

Assignments are introduced with the *set* keyword.  There are no
user-defined variables; values can only be assigned to variables
attached to backend, request or document objects.  Most of these are
typed, and the values assigned to them must have a compatible unit
suffix.

You can use the *set* keyword to arbitrary HTTP headers. You can
remove headers with the *remove* or *unset* keywords, which are
synonym.

You can use the *rollback* keyword to revert any changes to req at
any time.

The *synthetic* keyword is used to produce a synthetic response
body in vcl_error. It takes a single string as argument.

You can force a crash of the client process with the *panic* keyword.
*panic* takes a string as argument.

The ``return(action)`` keyword terminates the subroutine. *action* can be,
depending on context one of

* deliver
* error
* fetch
* hash
* hit_for_pass
* lookup
* ok
* pass
* pipe
* restart

Please see the list of subroutines to see what return actions are
available where.

VCL has if tests, but no loops.

The contents of another VCL file may be inserted at any point in the
code by using the *include* keyword followed by the name of the other
file as a quoted string.
