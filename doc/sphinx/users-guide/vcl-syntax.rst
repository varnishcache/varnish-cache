VCL Syntax
----------

VCL has inherited a lot from C and it reads much like simple C or Perl.

Blocks are delimited by curly braces, statements end with semicolons,
and comments may be written as in C, C++ or Perl according to your own
preferences.

Note that VCL doesn't contain any loops or jump statements.

This section provides an outline of the more important parts of the
syntax. For a full documentation of VCL syntax please see
:ref:`reference-vcl` in the reference.

Strings
~~~~~~~

Basic strings are enclosed in " ... ", and may not contain newlines.

Backslash is not special, so for instance in `regsub()` you do not need
to do the "count-the-backslashes" polka:::

  regsub("barf", "(b)(a)(r)(f)", "\4\3\2p") -> "frap"

Long strings are enclosed in {" ... "}. They may contain any character
including ", newline and other control characters except for the NUL
(0x00) character. If you really want NUL characters in a string there
is a VMOD that makes it possible to create such strings.

.. _vcl_syntax_acl:

Access control lists (ACLs)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

An ACL declaration creates and initializes a named access control list
which can later be used to match client addresses::

       acl local {
         "localhost";         // myself
         "192.0.2.0"/24;      // and everyone on the local network
         ! "192.0.2.23";      // except for the dialin router
       }

If an ACL entry specifies a host name which Varnish is unable to
resolve, it will match any address it is compared to. Consequently,
if it is preceded by a negation mark, it will reject any address it is
compared to, which may not be what you intended. If the entry is
enclosed in parentheses, however, it will simply be ignored.

To match an IP address against an ACL, simply use the match operator::

       if (client.ip ~ local) {
         return (pipe);
       }

Operators
~~~~~~~~~

The following operators are available in VCL. See the examples further
down for, uhm, examples.

= 
 Assignment operator.

== 
 Comparison.

~
 Match. Can either be used with regular expressions or ACLs.

!
 Negation.

&&
 Logical *and*

||
 Logical *or*


Subroutines
~~~~~~~~~~~

A subroutine is used to group code for legibility or reusability::

  sub pipe_if_local {
    if (client.ip ~ local) {
      return (pipe);
    }
  }


Subroutines in VCL do not take arguments, nor do they return values.

To call a subroutine, use the call keyword followed by the subroutine's name::

  call pipe_if_local;

Varnish has quite a few built in subroutines that are called for each
transaction as it flows through Varnish. These builtin subroutines are all
named `vcl_*`. Your own subroutines cannot start their name with `vcl\_`.

.. XXX:looks as bit funky as red text? benc
