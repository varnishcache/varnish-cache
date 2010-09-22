==========
varnishlog
==========

--------------------
Display Varnish logs
--------------------

:Author: Dag-Erling Smørgrav
:Author: Per Buer
:Date:   2010-05-31
:Version: 0.2
:Manual section: 1


SYNOPSIS
========

varnishlog [-a] [-b] [-C] [-c] [-D] [-d] [-I regex] [-i tag] [-k keep] 
[-n varnish_name] [-o] [-P file] [-r file] [-s num] [-u] [-V] 
[-w file] [-X regex] [-x tag] [tag regex]

DESCRIPTION
===========


The varnishlog utility reads and presents varnishd(1) shared memory logs.

The following options are available:

-a          When writing to a file, append to it rather than overwrite it.

-b          Include log entries which result from communication with a backend server.  
	    If neither -b nor -c is specified, varnishlog acts as if they both were.

-C          Ignore case when matching regular expressions.

-c          Include log entries which result from communication with a client.  
	    If neither -b nor -c is specified, varnishlog acts as if they both were.

-D          Daemonize.

-d          Process old log entries on startup.  Normally, varnishlog will only process entries 
	    which are written to the log after it starts.

-I regex    Include log entries which match the specified regular expression.  If 
   	    neither -I nor -i is specified, all log entries are included.

-i tag      Include log entries with the specified tag.  If neither -I nor -i is specified, 
   	    all log entries are included.

-k num      Only show the first num log records.

-n          Specifies the name of the varnishd instance to get logs from.  If -n is not 
	    specified, the host name is used.

-o          Group log entries by request ID.  This has no effect when writing to a 
	    file using the -w option.

-P file     Write the process's PID to the specified file.

-r file     Read log entries from file instead of shared memory.

-s num      Skip the first num log records.

-u          Unbuffered output.

-V          Display the version number and exit.

-w file     Write log entries to file instead of displaying them.  The file 
   	    will be overwritten unless the -a option was specified. If 
	    varnishlog receives a SIGHUP while writing to a file, it will 
	    reopen the file, allowing the old one to be rotated away.

-X regex    Exclude log entries which match the specified regular expression.

-x tag      Exclude log entries with the specified tag.

If the -o option was specified, an additional tag and regex may be
specified to select only requests which generated a log entry with the
given tag whose contents match the given regex.

TAGS
====
The following log entry tags are currently defined:

* Backend
* BackendClose
* BackendOpen
* BackendReuse
* BackendXID
* CLI
* ClientAddr
* Debug
* Error
* ExpBan
* ExpKill
* ExpPick
* Hit
* HitPass
* HttpError
* HttpGarbage
* Length
* ObjHeader
* ObjLostHeader
* ObjProtocol
* ObjRequest
* ObjResponse
* ObjStatus
* ObjURL
* ReqEnd
* ReqStart
* RxHeader
* RxLostHeader
* RxProtocol
* RxRequest
* RxResponse
* RxStatus
* RxURL
* SessionClose
* SessionOpen
* StatAddr
* StatSess
* TTL
* TxHeader
* TxLostHeader
* TxProtocol
* TxRequest
* TxResponse
* TxStatus
* TxURL
* VCL_acl
* VCL_call
* VCL_return
* VCL_trace
* WorkThread

EXAMPLES
========

The following command line simply copies all log entries to a log file:::

    $ varnishlog -w /var/log/varnish.log

The following command line reads that same log file and displays requests for the front page:::

    $ varnishlog -r /var/log/varnish.log -c -o RxURL '^/$'

SEE ALSO
========
* varnishd(1)
* varnishhist(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)

HISTORY
=======

The varnishlog utility was developed by Poul-Henning Kamp ⟨phk@phk.freebsd.dk⟩ in cooperation with Verdens Gang
AS, Linpro AS and Varnish Software.  This manual page was initially written by Dag-Erling Smørgrav.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2008 Linpro AS
* Copyright (c) 2008-2010 Redpill Linpro AS
* Copyright (c) 2010 Varnish Software AS
