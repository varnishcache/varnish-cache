..
	Copyright (c) 2022 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_routine:

========================
Getting into the routine
========================

Yesterday we released `VSV00009 </security/VSV00009.html>`_, a pretty
harmless DoS from the backend side, which could trivially be mitigated
in VCL.

By now handling security issues seem to have become routine for the
project, which is good, because that is the world we live in, and 
bad, because we live in a world where that is a necessary skill.

From the very start of the project, we have treated backends
as "trusted", in the sense that a lot of nasty stuff we try to handle
from clients got "dont do that then" treatment from the backend.

That was back when "cloud" were called "mainframes" and "containers"
were called "jails", way back when CDNs were only for companies
with more money than skill.

Part of the reasoning was also maximizing compatibility.

Backends were a lot more - let us call it "heterogenous" - back
then.  Some of them were literally kludges nailed to the side of
legacy newspaper production systems, and sometimes it was obvious
that they had not heard about RFCs.

For the problem we fixed yesterday, one line of VCL took care of
the problem, but that is not guaranteed to always be the case.

These days the "web" is a lot more regimented, and expecting
standards-compliance from backends makes sense, so we will
tighten the screws in that department as an ongoing activity.

Poul-Henning, 2022-08-05
