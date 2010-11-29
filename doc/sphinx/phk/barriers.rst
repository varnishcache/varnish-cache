.. _phk_barriers:

============================
Security barriers in Varnish
============================

Security is a very important design driver in Varnish, more likely than not,
if you find yourself thinking "Why did he do _that_ ? the answer has to
do with security.

The Varnish security model is based on some very crude but easy to understand
barriers between the various components::

                .-->- provides ->---------------------------------------.
                |                                          |            |
       (ADMIN)--+-->- runs ----->---.                      |            |
                |                   |                      |            |
                |-->- cli_req -->---|                      v            v
                '--<- cli_resp -<---|                     VCL        MODULE
                                    |                      |            |
       (OPER)                       |                      |reads       |
         |                          |                      |            |
         |runs                      |                      |            |
         |      .-<- create -<-.    |    .->- fork ->-.    v            |
         v      |->- check -->-|-- MGR --|            |-- VCC <- loads -|
        VSM     |-<- write --<-'    |    '-<- wait -<-'    |            |
       TOOLS    |                   |                      |            |
         ^      |     .-------------'                      |            |
         |      |     |                                    |writes      |
         |reads |     |->- fork ----->-.                   |            |
         |      |     |->- cli_req -->-|                   |            |
        VSM ----'     |-<- cli_resp -<-|                   v            |
         |            '-<- wait -----<-|                VCL.SO          |
         |                             |                   |            |
         |                             |                   |            |
         |---->----- inherit --->------|--<-- loads -------'            |
         |---->-----  reads ---->------|                                |
         '----<----- writes ----<------|--<-- loads --------------------'
                                       |
                                       |
                                       |
           .--->-- http_req --->--.    |    .-->-- http_req --->--.
  (ANON) --|                      |-- CLD --|                     |-- (BACKEND)
           '---<-- http_resp --<--'         '--<-- http_resp --<--'

(ASCII-ART rules!)

The really Important Barrier
============================

The central actor in Varnish is the Manager process, "MGR", which is the 
process the administrator "(ADMIN)" starts to get web-cache service.

Having been there myself, I do not subscribe to the "I feel cool and important
when I get woken up at 3AM to restart a dead process" school of thought, in
fact, I think that is a clear sign of mindless stupidity:  If we cannot
get a computer to restart a dead process, why do we even have them ?

The task of the Manager process is therefore not cache web content,
but to make sure there always is a process which does that, the
Child "CLD" process.

That is the major barrier in Varnish:  All management happens in
one process all actual movement of traffic happens in another, and
the Manager process does not trust the Child process at all.

The Child process is in a the totally unprotected domain:  Any
computer on the InterNet "(ANON)" can connect to the Child process
and ask for some web-object.

If John D. Criminal manages to exploit a security hole in Varnish, it is
the Child process he subverts.  If he carries out a DoS attack, it is
the Child process he tries to fell.

Therefore the Manager starts the Child with as low priviledge as practically
possible, and we close all filedescriptors it should not have access to and
so on.

There are only three channels of communication back to the Manager
process: An exit code, a CLI response or writing stuff into the
shared memory file "VSM" used for statistics and logging, all of
these are well defended by the Manager process.

The Admin/Oper Barrier
======================

If you look at the top left corner of the diagram, you will see that Varnish
operates with separate Administrator "(ADMIN)" and Operator "(OPER)" roles.

The Administrator does things, changes stuff etc.  The Operator keeps an
eye on things to make sure they are as they should be.

These days Operators are often scripts and data collection tools, and
there is no reason to assume they are bugfree, so Varnish does not
trust the Operator role, that is a pure one-way relationship.

(Trick:  If the Child process us run under user "nobody", you can
allow marginally trusted operations personel access to the "nobody"
account (for instance using .ssh/authorized_keys2), and they will
be able to kill the Child process, prompting the Manager process to
restart it again with the same parameters and settings.)

The Administrator has the final say, and of course, the administrator
can decide under which circumstances that authority will be shared.

Needless to say, if the system on which Varnish runs is not properly
secured, the Administrator's monopoly of control will be compromised.

All the other barriers
======================

There are more barriers, you can spot them by following the arrows in
the diagram, but they are more sort of "technical" than "political" and
generally try to guard against programming flaws as much as security
compromise.

For instance the VCC compiler runs in a separate child process, to make
sure that a memory leak or other flaw in the compiler does not accumulate
trouble for the Manager process.

Hope this explanation helps understand why Varnish is not just a single
process like all other server programs.

Poul-Henning, 2010-06-28
