.. _vcl-built-in-subs:

Built in subroutines
====================

Various built-in subroutines are called during processing of client-
and backend requests as well as upon ``vcl.load`` and ``vcl.discard``.

See :ref:`reference-states` for a detailed graphical overview of the
states and how they relate to core code functions and VCL subroutines.

Built-in subroutines always terminate with a ``return (<action>)``,
where ``<action>`` determines how processing continues in the request
processing state machine.

The behaviour of actions is identical or at least similar across
subroutines, so differences are only documented where relevant.

The :ref:`builtin_vcl` is documented in the VCL reference manual and
describes common actions and built-in subroutines.
