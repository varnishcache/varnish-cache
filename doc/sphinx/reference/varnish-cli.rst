=======
varnish
=======

------------------------------
Varnish Command Line Interface
------------------------------

:Author: Per Buer
:Date:   2011-03-23
:Version: 0.1
:Manual section: 7

DESCRIPTION
===========

Varnish as a command line interface (CLI) which can control and change
most of the operational parameters and the configuration of Varnish,
without interrupting the running service.

The CLI can be used for the following tasks:

configuration
     You can upload, change and delete VCL files from the CLI. 

parameters 
     You can inspect and change the various parameters Varnish has
     available through the CLI. The individual parameters are
     documented in the varnishd(1) man page.

statistics
     Statistic counters are available from the CLI.

bans 
     Bans are filters that are applied to keep Varnish from serving
     stale content. When you issue a ban Varnish will not serve any
     *banned* object from cache, but rather re-fetch it from it's back
     end servers.

process management
     You can stop and start the cache (child) process though the
     CLI. You can also retrieve the lastst stack trace if the child
     process has crashed.

If you invoke varnishd(1) with -T, -M or -d the CLI will be
available. In debug mode (-d) the CLI will be in the foreground, with
-T you can connect to it with varnishadm or telnet and with -M
varnishd will connect back to a listening service *pushing* the CLI to
that service. Please see varnishd(1) for details.


Syntax
------

Commands are usually terminated with a newline. Long command can be
entered using sh style *here documents*. The format of here-documents
is:::

   << word
	here document
   word

*word* can be any continuous string choosen to make sure it doesn't
appear naturally in the following *here document*.

When using the here document style of input there are no restrictions
on lenght. When using newline-terminated commands maximum lenght is
limited by the varnishd parameter *cli_buffer*.

When commands are newline terminated they get *tokenized* before
parsing so if you have significant spaces enclose your strings in
double quotes. Within the quotes you can escape characters with
\\. The \n, \r and \t get translated to newlines, carrage returns and
tabs. Double quotes themselves can be escaped with a backslash.

To enter characters in octals use the \\nnn syntax. Hexadecimals can
be entered with the \\xnn syntax.

Commands
--------

help [command]
      Display a list of available commands.

      If the command is specified, display help for this command.

param.set param value
      Set the parameter specified by param to the specified value.
      See Run-Time Parameters for a list of parame‐ ters.

param.show [-l] [param]
      Display a list if run-time parameters and their values.

      If the -l option is specified, the list includes a brief
      explanation of each parameter.

      If a param is specified, display only the value and explanation
      for this parameter.

ping  [timestamp]
      Ping the Varnish cache process, keeping the connection alive.

ban   *field operator argument* [&& field operator argument [...]]
      Immediately invalidate all documents matching the ban
      expression.  See *Ban Expressions* for more documentation and
      examples.

ban.list
      All requests for objects from the cache are matched against
      items on the ban list.  If an object in the cache is older than
      a matching ban list item, it is considered "banned", and will be
      fetched from the backend instead.

      When a ban expression is older than all the objects in the
      cache, it is removed from the list.

      ban.list displays the ban list. The output looks something like
      this (broken into two lines):

      0x7fea4fcb0580 1303835108.618863   131G   req.http.host ~ 
      www.myhost.com && req.url ~ /some/url

      The first field is the address of the ban. 

      The second is the time of entry into the list, given
      as a high precision timestamp.

      The third field describes many objects point to this ban. When
      an object is compared to a ban the object is marked with a
      reference to the newest ban it was tested against. This isn't
      really useful unless you're debugging.

      A "G" marks that the ban is "Gone". Meaning it has been marked
      as a duplicate or it is no longer valid. It stays in the list
      for effiency reasons.

      Then follows the actual ban it self.

ban.url regexp
      Immediately invalidate all documents whose URL matches the
      specified regular expression. Please note that the Host part of
      the URL is ignored, so if you have several virtual hosts all of
      them will be banned. Use *ban* to specify a complete ban if you
      need to narrow it down.

quit
      Close the connection to the varnish admin port.

start
      Start the Varnish cache process if it is not already running.

stats
      Show summary statistics.

      All the numbers presented are totals since server startup; for a
      better idea of the current situation, use the varnishstat(1)
      utility.

status
      Check the status of the Varnish cache process.

stop
      Stop the Varnish cache process.

vcl.discard configname
      Discard the configuration specified by configname.  This will
      have no effect if the specified configuration has a non-zero
      reference count.

vcl.inline configname vcl
      Create a new configuration named configname with the VCL code
      specified by vcl, which must be a quoted string.

vcl.list
      List available configurations and their respective reference
      counts.  The active configuration is indicated with an asterisk
      ("*").

vcl.load configname filename
      Create a new configuration named configname with the contents of
      the specified file.

vcl.show configname
      Display the source code for the specified configuration.

vcl.use configname
      Start using the configuration specified by configname for all
      new requests.  Existing requests will con‐ tinue using whichever
      configuration was in use when they arrived.

Ban Expressions
---------------

A ban expression consists of one or more conditions.  A condition
consists of a field, an operator, and an argument.  Conditions can be
ANDed together with "&&".

A field can be any of the variables from VCL, for instance req.url,
req.http.host or obj.set-cookie.

Operators are "==" for direct comparision, "~" for a regular
expression match, and ">" or "<" for size comparisons.  Prepending
an operator with "!" negates the expression.

The argument could be a quoted string, a regexp, or an integer.
Integers can have "KB", "MB", "GB" or "TB" appended for size related
fields.


Scripting
---------

If you are going to write a script that talks CLI to varnishd, the
include/cli.h contains the relevant magic numbers.

One particular magic number to know, is that the line with the status
code and length field always is exactly 13 characters long, including
the NL character.

For your reference the sourcefile lib/libvarnish/cli_common.h contains
the functions varnish code uses to read and write CLI response.

Details on authentication
-------------------------

If the -S secret-file is given as argument to varnishd, all network
CLI connections must authenticate, by proving they know the contents
of that file.

The file is read at the time the auth command is issued and the
contents is not cached in varnishd, so it is possible to update the
file on the fly.

Use the unix file permissions to control access to the file.

An authenticated session looks like this:::

   critter phk> telnet localhost 1234
   Trying ::1...
   Trying 127.0.0.1...
   Connected to localhost.
   Escape character is '^]'.
   107 59      
   ixslvvxrgkjptxmcgnnsdxsvdmvfympg
   
   Authentication required.
   
   auth 455ce847f0073c7ab3b1465f74507b75d3dc064c1e7de3b71e00de9092fdc89a
   200 193     
   -----------------------------
   Varnish HTTP accelerator CLI.
   -----------------------------
   Type 'help' for command list.
   Type 'quit' to close CLI session.
   Type 'start' to launch worker process.

The CLI status of 107 indicates that authentication is necessary. The
first 32 characters of the reponse text is the challenge
"ixsl...mpg". The challenge is randomly generated for each CLI
connection, and changes each time a 107 is emitted.

The most recently emitted challenge must be used for calculating the
authenticator "455c...c89a".

The authenticator is calculated by applying the SHA256 function to the
following byte sequence:

* Challenge string
* Newline (0x0a) character.
* Contents of the secret file
* Challenge string
* Newline (0x0a) character.

and dumping the resulting digest in lower-case hex.

In the above example, the secret file contained foo\n and thus:::

   critter phk> cat > _
   ixslvvxrgkjptxmcgnnsdxsvdmvfympg
   foo
   ixslvvxrgkjptxmcgnnsdxsvdmvfympg
   ^D
   critter phk> hexdump -C _
   00000000  69 78 73 6c 76 76 78 72  67 6b 6a 70 74 78 6d 63  |ixslvvxrgkjptxmc|
   00000010  67 6e 6e 73 64 78 73 76  64 6d 76 66 79 6d 70 67  |gnnsdxsvdmvfympg|
   00000020  0a 66 6f 6f 0a 69 78 73  6c 76 76 78 72 67 6b 6a  |.foo.ixslvvxrgkj|
   00000030  70 74 78 6d 63 67 6e 6e  73 64 78 73 76 64 6d 76  |ptxmcgnnsdxsvdmv|
   00000040  66 79 6d 70 67 0a                                 |fympg.|
   00000046
   critter phk> sha256 _ 
   SHA256 (_) = 455ce847f0073c7ab3b1465f74507b75d3dc064c1e7de3b71e00de9092fdc89a
   critter phk> openssl dgst -sha256 < _
   455ce847f0073c7ab3b1465f74507b75d3dc064c1e7de3b71e00de9092fdc89a

The sourcefile lib/libvarnish/cli_auth.c contains a useful function
which calculates the response, given an open filedescriptor to the
secret file, and the challenge string.

EXAMPLES
========

Simple example: All requests where req.url exactly matches the string
/news are banned from the cache:::

    req.url == "/news"

Example: Ban all documents where the name does not end with ".ogg",
and where the size of the object is greater than 10 megabytes:::

    req.url !~ "\.ogg$" && obj.size > 10MB

Example: Ban all documents where the serving host is "example.com"
or "www.example.com", and where the Set-Cookie header received from
the backend contains "USERID=1663":::

    req.http.host ~ "^(?i)(www\.)example.com$" && obj.set-cookie ~ "USERID=1663"

SEE ALSO
========

* varnishd(1)
* vanrishadm(1)
* vcl(7)

HISTORY
=======

The varnish manual page was written by Per Buer in 2011. Some of the
text was taken from the Varnish Cache wiki, the varnishd(7) man page
or the varnish source code.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2011 Varnish Software AS
