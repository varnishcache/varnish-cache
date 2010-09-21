.. _phk_vcl_expr:

===============
VCL Expressions
===============

I have been working on VCL expressions recently, and we are approaching
the home stretch now.

The data types in VCL are "sort of weird" seen with normal programming
language eyes, in that they are not "general purpose" types, but
rather tailored types for the task at hand.

For instance, we have both a TIME and a DURATION type, a quite
unusual constellation for a programming language.

But in HTTP context, it makes a lot of sense, you really have to
keep track of what is a relative time (age) and what is absolute
time (Expires).

Obviously, you can add a TIME and DURATION, the result is a TIME.

Equally obviously, you can not add TIME to TIME, but you can subtract
TIME from TIME, resulting in a DURATION.

VCL do also have "naked" numbers, like INT and REAL, but what you
can do with them is very limited.  For instance you can multiply a
duration by a REAL, but you can not multiply a TIME by anything.

Given that we have our own types, the next question is what
precedence operators have.

The C programming language is famous for having a couple of gottchas
in its precedence rules and given our limited and narrow type
repetoire, blindly importing a set of precedence rules may confuse
a lot more than it may help.

Here are the precedence rules I have settled on, from highest to
lowest precedence:

Atomic
	'true', 'false', constants

	function calls

	variables

	'(' expression ')'

Multiply/Divide
	INT * INT

	INT / INT

	DURATION * REAL

Add/Subtract
	STRING + STRING

	INT +/- INT

	TIME +/- DURATION

	TIME - TIME

	DURATION +/- DURATION

Comparisons
	'==', '!=', '<', '>', '~' and '!~'

	string existence check (-> BOOL)

Boolean not
	'!'

Boolean and
	'&&'

Boolean or
	'||'


Input and feedback most welcome!

Until next time,

Poul-Henning, 2010-09-21
