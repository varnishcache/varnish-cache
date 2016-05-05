.. _dev-guide-index:

The Varnish Developers Guide
============================

This is the deliberately short and to the point list of things
Varnish Developers should know.

Behaviour
---------

* Be sensible.

* If in doubt, think.

* If still in doubt, ask.

* Admit your mistakes, it's faster that way.

* Thou SHALL not paint `bikesheds. <http://bikeshed.org/>`_

* We will toss you out of the project rather than add another rule.

Technical stuff
----------------

* Our coding style guideline is FreeBSD's
  `style(9) <https://www.freebsd.org/cgi/man.cgi?query=style&sektion=9>`_

* See autogen.des script for developer options to the toolchain.

* We always -Werror, there are no harmless warnings, only source code
  that does not express intent well enough.

* We prefer the source code, rather than the comments explain what is
  going on, that way tools like FlexeLint and Coverity also gets a chance.

* Our reference platforms are Ubuntu and FreeBSD.

* Asserts have negative cost, they save developer time next time around.

* Our license is BSD 2-clause or looser, no GPL or LGPL.

* We havn't had a major security issue in 10 years, and we're not going
  to start having them now.

Bugs, issues, feature requests & VIPs
-------------------------------------

Bugs, issues and feature requests start out as github issues.

We do a "bug-wash" every monday at 13:00 EU time, where new and otherwise
worthy of discussion issues are pow-wowed.

Tickets we cannot do anything about are closed.

If feature-requests make sense, they get moved to a wiki/VIP page until
somebody implements them.

Varnishtest cases for bugs is the norm, not the exception.

Architectural stuff
-------------------

These rules are imported from the X11 project:

* It is as important to decide what a system is not as to decide what it is.

* Do not serve all the world's needs; rather, make the system extensible so
  that additional needs can be met in an upwardly compatible fashion.

* The only thing worse than generalizing from one example is generalizing
  from no examples at all.

* If a problem is not completely understood, it is probably best to provide
  no solution at all.

* If you can get 90 percent of the desired effect for 10 percent of the work,
  use the simpler solution.

* Isolate complexity as much as possible.

* Provide mechanism, rather than policy.

