.. _phk_sphinx:

===================================
Why Sphinx_ and reStructuredText_ ?
===================================

The first school of thought on documentation, is the one we subscribe
to in Varnish right now: "Documentation schmocumentation..."  It does
not work for anybody.

The second school is the "Write a {La}TeX document" school, where
the documentation is seen as a stand alone product, which is produced
independently.  This works great for PDF output, and sucks royally
for HTML and TXT output.

The third school is the "Literate programming" school, which abandons
readability of *both* the program source code *and* the documentation
source, which seems to be one of the best access protections
one can put on the source code of either.

The fourth school is the "DoxyGen" school, which lets a program
collect a mindless list of hyperlinked variable, procedure, class
and filenames, and call that "documentation".

And the fifth school is anything that uses a fileformat that
cannot be put into a version control system, because it is
binary and non-diff'able.  It doesn't matter if it is
OpenOffice, LyX or Word, a non-diffable doc source is a no go
with programmers.

Quite frankly, none of these works very well in practice.

One of the very central issues, is that writing documentation must
not become a big and clear context-switch from programming.  That
precludes special graphical editors, browser-based (wiki!) formats
etc.

Yes, if you write documentation for half your workday, that works,
but if you write code most of your workday, that does not work.
Trust me on this, I have 25 years of experience avoiding using such
tools.

I found one project which has thought radically about the problem,
and their reasoning is interesting, and quite attractive to me:

#. TXT files are the lingua franca of computers, even if
   you are logged with TELNET using IP over Avian Carriers
   (Which is more widespread in Norway than you would think)
   you can read documentation in a .TXT format.

#. TXT is the most restrictive typographical format, so
   rather than trying to neuter a high-level format into .TXT,
   it is smarter to make the .TXT the source, and reinterpret
   it structurally into the more capable formats.

In other words: we are talking about the ReStructuredText_ of the
Python project, as wrapped by the Sphinx_ project.

Unless there is something I have totally failed to spot, that is
going to be the new documentation platform in Varnish.

Take a peek at the Python docs, and try pressing the "show source"
link at the bottom of the left menu:

(link to random python doc page:)

        http://docs.python.org/py3k/reference/expressions.html

Dependency wise, that means you can edit docs with no special
tools, you need python+docutils+sphinx to format HTML and a LaTex
(pdflatex ?) to produce PDFs, something I only expect to happen
on the project server on a regular basis.

I can live with that, I might even rewrite the VCC scripts
from Tcl to Python in that case.

Poul-Henning, 2010-04-11


.. _Sphinx: http://sphinx.pocoo.org/
.. _reStructuredText: http://docutils.sourceforge.net/rst.html
