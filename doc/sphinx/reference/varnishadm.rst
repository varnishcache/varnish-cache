==========
varnishadm
==========

----------------------------------
Control a running varnish instance
----------------------------------

:Author: Cecilie Fritzvold
:Author: Per Buer
:Date:   2010-05-31
:Version: 0.2
:Manual section: 1

SYNOPSIS
========

       varnishadm [-t timeout] [-S secret_file] -T address:port [command [...]]

DESCRIPTION
===========

The varnishadm utility establishes a CLI connection using the -T and -S arguments.

If a command is given, the command and arguments are sent over the CLI
connection and the result returned on stdout.

If no command argument is given varnishadm will pass commands and
replies between the CLI socket and stdin/stdout.

OPTIONS
=======

-t timeout               Wait no longer than this many seconds for an operation to finish.

-S secret_file           Specify the authentication secret file. This should be the same -S 
                         argument as was given to varnishd. Only processes which can read 
                         the contents of this file, will be able to authenticate the CLI connection.

-T address:port          Connect to the management interface at the specified address and port.


Available commands and parameters are documented in the varnishd(1)
manual page.  Additionally, a summary of commands can be obtained by
issuing the *help* command, and a summary of parameters can be
obtained by issuing the *param.show* command.

EXIT STATUS
===========

If a command is given, the exit status of the varnishadm utility is
zero if the command succeeded, and non-zero otherwise.

EXAMPLES
========

Some ways you can use varnishadm:::

           varnishadm -T localhost:999 -S /var/db/secret vcl.use foo
           echo vcl.use foo | varnishadm -T localhost:999 -S /var/db/secret
           echo vcl.use foo | ssh vhost varnishadm -T localhost:999 -S /var/db/secret

SEE ALSO
========

* varnishd(1)

HISTORY
=======

The varnishadm utility and this manual page were written by Cecilie
Fritzvold. Converted to reStructured and updated in 2010 by Per
Buer.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2008 Linpro AS
* Copyright (c) 2008-2010 Redpill Linpro AS
* Copyright (c) 2010 Varnish Software AS
