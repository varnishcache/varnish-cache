.. _tutorial-web_accelerator:

What is a web accelerator
-------------------------


The problem
-----------

Application servers  er slow. They do lots of different things and
they can sometimes take seconds to complete a web page.

The solution
------------

Enter Varnish. It keeps a copy of the web pages that pass through
it. If it finds that it can reuse these later so the server doesn't
have to regenerate these it speeds things up.



