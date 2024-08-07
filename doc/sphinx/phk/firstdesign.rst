..
	Copyright (c) 2016-2018 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_firstdesign:

===========================
The first design of Varnish
===========================

I have been working on a "bit-storage" facility for datamuseum.dk,
and as part of my "eat your own dog-food" policy, I converting my
own personal archive (41 DVD's worth) as a test.

Along the way I passed through 2006 and found some files from
the birth of Varnish 10 years ago.

The first Varnish Design notes
------------------------------

This file are notes taken during a meeting in Oslo on 2nd Feb 2006,
which in essence consisted of Anders Berg cursing Squid for a couple
of hours.

(Originally the meeting was scheduled for Jan 24th but a SAS pilot
strike put an end to that.)

To be honest I knew very little about web-traffic, my own homepage
was written in HTML in vi(1), so I had a bit of catching up to do
on that front, but the overall job was pretty simple:  A program
to move bytes ... fast.

It is quite interesting to see how many things we got right and
where we kept thinking in the old frame of reference (ie: Squid)::

	Notes on Varnish
	----------------

	Collected 2006-02-02 to 2006-02-..

	Poul-Henning Kamp


	Philosophy
	----------

	It is not enough to deliver a technically superior piece of software,
	if it is not possible for people to deploy it usefully in a sensible
	way and timely fashion.


	Deployment scenarios
	--------------------

	There are two fundamental usage scenarios for Varnish: when the
	first machine is brought up to offload a struggling backend and
	when a subsequent machine is brought online to help handle the load.


	The first (layer of) Varnish
	----------------------------

	Somebodys webserver is struggling and they decide to try Varnish.

	Often this will be a skunkworks operation with some random PC
	purloined from wherever it wasn't being used and the Varnish "HOWTO"
	in one hand.

	If they do it in an orderly fashion before things reach panic proportions,
	a sensible model is to setup the Varnish box, test it out from your
	own browser, see that it answers correctly.  Test it some more and
	then add the IP# to the DNS records so that it takes 50% of the load
	off the backend.

	If it happens as firefighting at 3AM the backend will be moved to another
	IP, the Varnish box given the main IP and things had better work real
	well, really fast.

	In both cases, it would be ideal if all that is necessary to tell
	Varnish are two pieces of information:

		Storage location
			Alternatively we can offer an "auto" setting that makes
			Varnish discover what is available and use what it find.

		DNS or IP# of backend.

			IP# is useful when the DNS settings are not quite certain
			or when split DNS horizon setups are used.

	Ideally this can be done on the commandline so that there is no
	configuration file to edit to get going, just

		varnish -d /home/varnish -s backend.example.dom

	and you're off running.

	A text, curses or HTML based facility to give some instant
	feedback and stats is necessary.

	If circumstances are not conductive to strucured approach, it should
	be possible to repeat this process and set up N independent Varnish
	boxes and get some sort of relief without having to read any further
	documentation.


	The subsequent (layers of) Varnish
	----------------------------------

	This is what happens once everybody has caught their breath,
	and where we start to talk about Varnish clusters.

	We can assume that at this point, the already installed Varnish
	machines have been configured more precisely and that people
	have studied Varnish configuration to some level of detail.

	When Varnish machines are put in a cluster, the administrator should
	be able to consider the cluster as a unit and not have to think and
	interact with the individual nodes.

	Some sort of central management node or facility must exist and
	it would be preferable if this was not a physical but a logical
	entity so that it can follow the admin to the beach.  Ideally it
	would give basic functionality in any browser, even mobile phones.

	The focus here is scaleability, we want to avoid per-machine
	configuration if at all possible.  Ideally, preconfigured hardware
	can be plugged into power and net, find an address with DHCP, contact
	preconfigured management node, get a configuration and start working.

	But we also need to think about how we avoid a site of Varnish
	machines from acting like a stampeeding horde when the power or
	connectivity is brought back after a disruption.  Some sort of
	slow starting ("warm-up" ?) must be implemented to prevent them
	from hitting all the backend with the full force.

	An important aspect of cluster operations is giving a statistically
	meaninful judgement of the cluster size, in particular answering
	the question "would adding another machine help ?" precisely.

	We should have a facility that allows the administrator to type
	in a REGEXP/URL and have all the nodes answer with a checksum, age
	and expiry timer for any documents they have which match.  The
	results should be grouped by URL and checksum.


	Technical concepts
	------------------

	We want the central Varnish process to be that, just one process, and
	we want to keep it small and efficient at all cost.

	Code that will not be used for the central functionality should not
	be part of the central process.  For instance code to parse, validate
	and interpret the (possibly) complex configuration file should be a
	separate program.

	Depending on the situation, the Varnish process can either invoke
	this program via a pipe or receive the ready to use data structures
	via a network connection.

	Exported data from the Varnish process should be made as cheap as
	possible, likely shared memory.  That will allow us to deploy separate
	processes for log-grabbing, statistics monitoring and similar
	"off-duty" tasks and let the central process get on with the
	important job.


	Backend interaction
	-------------------

	We need a way to tune the backend interaction further than what the
	HTTP protocol offers out of the box.

	We can assume that all documents we get from the backend has an
	expiry timer, if not we will set a default timer (configurable of
	course).

	But we need further policy than that.  Amongst the questions we have
	to ask are:

		How long time after the expiry can we serve a cached copy
		of this document while we have reason to believe the backend
		can supply us with an update ?

		How long time after the expiry can we serve a cached copy
		of this document if the backend does not reply or is
		unreachable.

		If we cannot serve this document out of cache and the backend
		cannot inform us, what do we serve instead (404 ?  A default
		document of some sort ?)

		Should we just not serve this page at all if we are in a
		bandwidth crush (DoS/stampede) situation ?

	It may also make sense to have a "emergency detector" which triggers
	when the backend is overloaded and offer a scaling factor for all
	timeouts for when in such an emergency state.  Something like "If
	the average response time of the backend rises above 10 seconds,
	multiply all expiry timers by two".

	It probably also makes sense to have a bandwidth/request traffic
	shaper for backend traffic to prevent any one Varnish machine from
	pummeling the backend in case of attacks or misconfigured
	expiry headers.


	Startup/consistency
	-------------------

	We need to decide what to do about the cache when the Varnish
	process starts.  There may be a difference between it starting
	first time after the machine booted and when it is subsequently
	(re)started.

	By far the easiest thing to do is to disregard the cache, that saves
	a lot of code for locating and validating the contents, but this
	carries a penalty in backend or cluster fetches whenever a node
	comes up.  Lets call this the "transient cache model"

	The alternative is to allow persistently cached contents to be used
	according to configured criteria:

		Can expired contents be served if we can't contact the
		backend ?  (dangerous...)

		Can unexpired contents be served if we can't contact the
		backend ?  If so, how much past the expiry ?

	It is a very good question how big a fraction of the persistent
	cache would be usable after typical downtimes:

		After a Varnish process restart:  Nearly all.

		After a power-failure ?  Probably at least half, but probably
		not the half that contains the most busy pages.

	And we need to take into consideration if validating the format and
	contents of the cache might take more resources and time than getting
	the content from the backend.

	Off the top of my head, I would prefer the transient model any day
	because of the simplicity and lack of potential consistency problems,
	but if the load on the back end is intolerable this may not be
	practically feasible.

	The best way to decide is to carefully analyze a number of cold
	starts and cache content replacement traces.

	The choice we make does affect the storage management part of Varnish,
	but I see that is being modular in any instance, so it may merely be
	that some storage modules come up clean on any start while other
	will come up with existing objects cached.


	Clustering
	----------

	I'm somewhat torn on clustering for traffic purposes.  For admin
	and management: Yes, certainly, but starting to pass objects from
	one machine in a cluster to another is likely to be just be a waste
	of time and code.

	Today one can trivially fit 1TB into a 1U machine so the partitioning
	argument for cache clusters doesn't sound particularly urgent to me.

	If all machines in the cluster have sufficient cache capacity, the
	other remaining argument is backend offloading, that would likely
	be better mitigated by implementing a 1:10 style two-layer cluster
	with the second level node possibly having twice the storage of
	the front row nodes.

	The coordination necessary for keeping track of, or discovering in
	real-time, who has a given object can easily turn into a traffic
	and cpu load nightmare.

	And from a performance point of view, it only reduces quality:
	First we send out a discovery multicast, then we wait some amount
	of time to see if a response arrives only then should we start
	to ask the backend for the object.  With a two-level cluster
	we can ask the layer-two node right away and if it doesn't have
	the object it can ask the back-end right away, no timeout is
	involved in that.

	Finally Consider the impact on a cluster of a "must get" object
	like an IMG tag with a misspelled URL.  Every hit on the front page
	results in one get of the wrong URL.  One machine in the cluster
	ask everybody else in the cluster "do you have this URL" every
	time somebody gets the frontpage.

	If we implement a negative feedback protocol ("No I don't"), then
	each hit on the wrong URL will result in N+1 packets (assuming multicast).

	If we use a silent negative protocol the result is less severe for
	the machine that got the request, but still everybody wakes up to
	to find out that no, we didn't have that URL.

	Negative caching can mitigate this to some extent.


	Privacy
	-------

	Configuration data and instructions passed forth and back should
	be encrypted and signed if so configured.  Using PGP keys is
	a very tempting and simple solution which would pave the way for
	administrators typing a short ascii encoded pgp signed message
	into a SMS from their Bahamas beach vacation...


	Implementation ideas
	--------------------

	The simplest storage method mmap(2)'s a disk or file and puts
	objects into the virtual memory on page aligned boundaries,
	using a small struct for metadata.  Data is not persistant
	across reboots.  Object free is incredibly cheap.  Object
	allocation should reuse recently freed space if at all possible.
	"First free hole" is probably a good allocation strategy.
	Sendfile can be used if filebacked.  If nothing else disks
	can be used by making a 1-file filesystem on them.

	More complex storage methods are object per file and object
	in database models.  They are relatively trival and well
	understood.  May offer persistence.

	Read-Only storage methods may make sense for getting hold
	of static emergency contents from CD-ROM etc.

	Treat each disk arm as a separate storage unit and keep track of
	service time (if possible) to decide storage scheduling.

	Avoid regular expressions at runtime.  If config file contains
	regexps, compile them into executable code and dlopen() it
	into the Varnish process.  Use versioning and refcounts to
	do memory management on such segments.

	Avoid committing transmit buffer space until we have bandwidth
	estimate for client.  One possible way:  Send HTTP header
	and time ACKs getting back, then calculate transmit buffer size
	and send object.  This makes DoS attacks more harmless and
	mitigates traffic stampedes.

	Kill all TCP connections after N seconds, nobody waits an hour
	for a web-page to load.

	Abuse mitigation interface to firewall/traffic shaping:  Allow
	the central node to put an IP/Net into traffic shaping or take
	it out of traffic shaping firewall rules.  Monitor/interface
	process (not main Varnish process) calls script to config
	firewalling.

	"Warm-up" instructions can take a number of forms and we don't know
	what is the most efficient or most usable.  Here are some ideas:

	    Start at these URL's then...

		... follow all links down to N levels.

		... follow all links that match REGEXP no deeper than N levels down.

		... follow N random links no deeper than M levels down.

		... load N objects by following random links no deeper than
		    M levels down.

	    But...

		... never follow any links that match REGEXP

		... never pick up objects larger than N bytes

		... never pick up objects older than T seconds


	It makes a lot of sense to not actually implement this in the main
	Varnish process, but rather supply a template perl or python script
	that primes the cache by requesting the objects through Varnish.
	(That would require us to listen separately on 127.0.0.1
	so the perlscript can get in touch with Varnish while in warm-up.)

	One interesting but quite likely overengineered option in the
	cluster case is if the central monitor tracks a fraction of the
	requests through the logs of the running machines in the cluster,
	spots the hot objects and tell the warming up varnish what objects
	to get and from where.


	In the cluster configuration, it is probably best to run the cluster
	interaction in a separate process rather than the main Varnish
	process.  From Varnish to cluster info would go through the shared
	memory, but we don't want to implement locking in the shmem so
	some sort of back-channel (UNIX domain or UDP socket ?) is necessary.

	If we have such an "supervisor" process, it could also be tasked
	with restarting the varnish process if vitals signs fail:  A time
	stamp in the shmem or kill -0 $pid.

	It may even make sense to run the "supervisor" process in stand
	alone mode as well, there it can offer a HTML based interface
	to the Varnish process (via shmem).

	For cluster use the user would probably just pass an extra argument
	when he starts up Varnish:

		varnish -c $cluster_args $other_args
	vs

		varnish $other_args

	and a "varnish" shell script will Do The Right Thing.


	Shared memory
	-------------

	The shared memory layout needs to be thought about somewhat.  On one
	hand we want it to be stable enough to allow people to write programs
	or scripts that inspect it, on the other hand doing it entirely in
	ascii is both slow and prone to race conditions.

	The various different data types in the shared memory can either be
	put into one single segment(= 1 file) or into individual segments
	(= multiple files).  I don't think the number of small data types to
	be big enough to make the latter impractical.

	Storing the "big overview" data in shmem in ASCII or HTML would
	allow one to point cat(1) or a browser directly at the mmaped file
	with no interpretation necessary, a big plus in my book.

	Similarly, if we don't update them too often, statistics could be stored
	in shared memory in perl/awk friendly ascii format.

	But the logfile will have to be (one or more) FIFO logs, probably at least
	three in fact:  Good requests, Bad requests, and exception messages.

	If we decide to make logentries fixed length, we could make them ascii
	so that a simple "sort -n /tmp/shmem.log" would put them in order after
	a leading numeric timestamp, but it is probably better to provide a
	utility to cat/tail-f the log and keep the log in a bytestring FIFO
	format.  Overruns should be marked in the output.


	*END*

The second Varnish Design notes
-------------------------------

You will notice above that there is no mention of VCL, it took a
couple of weeks for that particular lightning to strike.

Interestingly I know exactly where the lightning came from, and
what it hit.

The timeframe was around GCC 4.0.0 which was not their best release,
and I had for some time been pondering a pre-processor for the C
language to make up for the ISO-C stagnation and braindamage.

I've read most of the "classic" compiler books, and probably read
more compilers many people (Still to go: `GIER Algol 4 <http://datamuseum.dk/wiki/GIER/GA4GuideToDocumentationAndCode>`_) but to be honest I found
them far too theoretical and not very helpful from a *practical* compiler
construction point of view.

But there is one compiler-book which takes an entirely different
take:  `Hanson and Fraser's LCC book. <http://www.amazon.com/gp/search/?field-isbn=0805316701>`_ which throws LEX and YACC under the truck and
concentrates on compiling.

Taking their low-down approach to parsing, and emitting C code,
there really isn't much compiler left to write, and I had done
several interesting hacks towards my 'K' language.

The lightning rod was all the ideas Anders had for how Varnish
should be able to manipulate the traffic passing through, how
to decide what to cache, how long time to cache it, where to
cache it and ... it sounded like a lot of very detailed code
which had to be incredibly configurable.

Soon those two inspirations collided::


	Notes on Varnish
	----------------

	Collected 2006-02-24 to 2006-02-..

	Poul-Henning Kamp

	-----------------------------------------------------------------------
	Policy Configuration

	Policy is configured in a simple unidirectional (no loops, no goto)
	programming language which is compiled into 'C' and from there binary
	modules which are dlopen'ed by the main Varnish process.

	The dl object contains one exported symbol, a pointer to a structure
	which contains a reference count, a number of function pointers,
	a couple of string variables with identifying information.

	All access into the config is protected by the reference counts.

	Multiple policy configurations can be loaded at the same time
	but only one is the "active configuration".  Loading, switching and
	unloading of policy configurations happen via the management
	process.

	A global config sequence number is incremented on each switch and
	policy modified object attributes (ttl, cache/nocache) are all
	qualified by the config-sequence under which they were calculated
	and invalid if a different policy is now in effect.

	-----------------------------------------------------------------------
	Configuration Language

	XXX: include lines.

	BNF:
		program:	function
				| program function

		function:	"sub" function_name compound_statement

		compound_statement:	"{" statements "}"

		statements:	/* empty */
				| statement
				| statements statement


		statement:	if_statement
				| call_statement
				| "finish"
				| assignment_statement
				| action_statement

		if_statement:	"if" condition compound_statement elif_parts else_part

		elif_parts:	/* empty */
				| elif_part
				| elif_parts elif_part

		elif_part:	"elseif" condition compound_statement
				| "elsif" condition compound_statement
				| "else if" condition compound_statement

		else_part:	/* empty */
				| "else" compound_statement

		call_statement:	"call" function_name

		assign_statement:	field "=" value

		field:		object
				field "." variable

		action_statement:	action arguments

		arguments:	/* empty */
				arguments | argument

	-----------------------------------------------------------------------
	Sample request policy program

		sub request_policy {

			if (client.ip in 10.0.0.0/8) {
				no-cache
				finish
			}

			if (req.url.host ~ "cnn.no$") {
				rewrite	s/cnn.no$/vg.no/
			}

			if (req.url.path ~ "cgi-bin") {
				no-cache
			}

			if (req.useragent ~ "spider") {
				no-new-cache
			}

			if (backend.response_time > 0.8s) {
				set req.ttlfactor = 1.5
			} elseif (backend.response_time > 1.5s) {
				set req.ttlfactor = 2.0
			} elseif (backend.response_time > 2.5s) {
				set req.ttlfactor = 5.0
			}

			/*
			 * the program contains no references to
			 * maxage, s-maxage and expires, so the
			 * default handling (RFC2616) applies
			 */
		}

	-----------------------------------------------------------------------
	Sample fetch policy program

		sub backends {
			set backend.vg.ip = {...}
			set backend.ads.ip = {...}
			set backend.chat.ip = {...}
			set backend.chat.timeout = 10s
			set backend.chat.bandwidth = 2000 MB/s
			set backend.other.ip = {...}
		}

		sub vg_backend {
			set backend.ip = {10.0.0.1-5}
			set backend.timeout = 4s
			set backend.bandwidth = 2000Mb/s
		}

		sub fetch_policy {

			if (req.url.host ~ "/vg.no$/") {
				set req.backend = vg
				call vg_backend
			} else {
				/* XXX: specify 404 page url ? */
				error 404
			}

			if (backend.response_time > 2.0s) {
				if (req.url.path ~ "/landbrugspriser/") {
					error 504
				}
			}
			fetch
			if (backend.down) {
				if (obj.exist) {
					set obj.ttl += 10m
					finish
				}
				switch_config ohhshit
			}
			if (obj.result == 404) {
				error 300 "http://www.vg.no"
			}
			if (obj.result != 200) {
				finish
			}
			if (obj.size > 256k) {
				no-cache
			} else if (obj.size > 32k && obj.ttl < 2m) {
				obj.tll = 5m
			}
			if (backend.response_time > 2.0s) {
				set ttl *= 2.0
			}
		}

		sub prefetch_policy {

			if (obj.usage < 10 && obj.ttl < 5m) {
				fetch
			}
		}

	-----------------------------------------------------------------------
	Purging

	When a purge request comes in, the regexp is tagged with the next
	generation number and added to the tail of the list of purge regexps.

	Before a sender transmits an object, it is checked against any
	purge-regexps which have higher generation number than the object
	and if it matches the request is sent to a fetcher and the object
	purged.

	If there were purge regexps with higher generation to match, but
	they didn't match, the object is tagged with the current generation
	number and moved to the tail of the list.

	Otherwise, the object does not change generation number and is
	not moved on the generation list.

	New Objects are tagged with the current generation number and put
	at the tail of the list.

	Objects are removed from the generation list when deleted.

	When a purge object has a lower generation number than the first
	object on the generation list, the purge object has been completed
	and will be removed.  A log entry is written with number of compares
	and number of hits.

	-----------------------------------------------------------------------
	Random notes

		swap backed storage

		slowstart by config-flipping
			start-config has peer servers as backend
			once hitrate goes above limit, management process
			flips config to 'real' config.

		stat-object
			always URL, not regexp

		management + varnish process in one binary, comms via pipe

		Change from config with long expiry to short expiry, how
		does the ttl drop ?  (config sequence number invalidates
		all calculated/modified attributes.)

		Mgt process holds copy of acceptor socket ->  Restart without
		lost client requests.

		BW limit per client IP: create shortlived object (<4sec)
		to hold status.  Enforce limits by delaying responses.


	-----------------------------------------------------------------------
	Source structure


		libvarnish
			library with interface facilities, for instance
			functions to open&read shmem log

		varnish
			varnish sources in three classes

	-----------------------------------------------------------------------
	protocol cluster/mgt/varnish

	object_query url -> TTL, size, checksum
	{purge,invalidate} regexp
	object_status url -> object metadata

	load_config filename
	switch_config configname
	list_configs
	unload_config

	freeze 	# stop the clock, freezes the object store
	thaw

	suspend	# stop acceptor accepting new requests
	resume

	stop	# forced stop (exits) varnish process
	start
	restart = "stop;start"

	ping $utc_time -> pong $utc_time

	# cluster only
	config_contents filename $inline -> compilation messages

	stats [-mr] -> $data

	zero stats

	help

	-----------------------------------------------------------------------
	CLI (local)
		import protocol from above

		telnet localhost someport
		authentication:
			password $secret
		secret stored in {/usr/local}/etc/varnish.secret (400 root:wheel)


	-----------------------------------------------------------------------
	HTML (local)

		php/cgi-bin thttpd ?
		(alternatively direct from C-code.)
		Everything the CLI can do +
		stats
			popen("rrdtool");
		log view

	-----------------------------------------------------------------------
	CLI (cluster)
		import protocol from above, prefix machine/all
		compound stats
		accept / deny machine (?)
		curses if you set termtype

	-----------------------------------------------------------------------
	HTML (cluster)
		ditto
		ditto

		http://clustercontrol/purge?regexp=fslkdjfslkfdj
			POST with list of regexp
			authentication ? (IP access list)

	-----------------------------------------------------------------------
	Mail (cluster)

		pgp signed emails with CLI commands

	-----------------------------------------------------------------------
	connection varnish -> cluster controller

		Encryption
			SSL
		Authentication (?)
			IP number checks.

		varnish -c clusterid -C mycluster_ctrl.vg.no

	-----------------------------------------------------------------------
	Filer
		/usr/local/sbin/varnish
			contains mgt + varnish process.
			if -C argument, open SSL to cluster controller.
			Arguments:
				-p portnumber
				-c clusterid@cluster_controller
				-f config_file
				-m memory_limit
				-s kind[,storage-options]
				-l logfile,logsize
				-b backend ip...
				-d debug
				-u uid
				-a CLI_port

			KILL SIGTERM	-> suspend, stop

		/usr/local/sbin/varnish_cluster
			Cluster controller.
			Use syslog

			Arguments:
				-f config file
				-d debug
				-u uid (?)

		/usr/local/sbin/varnish_logger
			Logfile processor
			-i shmemfile
			-e regexp
			-o "/var/log/varnish.%Y%m%d.traffic"
			-e regexp2
			-n "/var/log/varnish.%Y%m%d.exception"  (NCSA format)
			-e regexp3
			-s syslog_level,syslogfacility
			-r host:port	send via TCP, prefix hostname

			SIGHUP: reopen all files.

		/usr/local/bin/varnish_cli
			Command line tool.

		/usr/local/share/varnish/etc/varnish.conf
			default request + fetch + backend scripts

		/usr/local/share/varnish/etc/rfc2616.conf
			RFC2616 compliant handling function

		/usr/local/etc/varnish.conf (optional)
			request + fetch + backend scripts

		/usr/local/share/varnish/etc/varnish.startup
			default startup sequence

		/usr/local/etc/varnish.startup (optional)
			startup sequence

		/usr/local/etc/varnish_cluster.conf
			XXX

		{/usr/local}/etc/varnish.secret
			CLI password file.

	-----------------------------------------------------------------------
	varnish.startup

		load config /foo/bar startup_conf
		switch config startup_conf
		!mypreloadscript
		load config /foo/real real_conf
		switch config real_conf
		resume


The third Varnish Design notes
-------------------------------

A couple of days later the ideas had gel'ed::


	Notes on Varnish
	----------------

	Collected 2006-02-26 to 2006-03-..

	Poul-Henning Kamp

	-----------------------------------------------------------------------

	Objects available to functions in VCL

		client	# The client

		req	# The request

		obj	# The object from which we satisfy it

		backend	# The chosen supplier

	-----------------------------------------------------------------------
	Configuration Language

	XXX: declare IP lists ?

	BNF:
		program:	part
				| program part

		part:		"sub" function_name compound
				| "backend" backend_name compound

		compound:	"{" statements "}"

		statements:	/* empty */
				| statement
				| statements statement

		statement:	conditional
				| functioncall
				| "set" field value
				| field "=" value
				| "no_cache"
				| "finish"
				| "no_new_cache"
				| call function_name
				| fetch
				| error status_code
				| error status_code string(message)
				| switch_config config_id
				| rewrite field string(match) string(replace)

		conditional:	"if" condition compound elif_parts else_part

		elif_parts:	/* empty */
				| elif_part
				| elif_parts elif_part

		elif_part:	"elseif" condition compound
				| "elsif" condition compound
				| "else if" condition compound

		else_part:	/* empty */
				| "else" compound

		functioncall:	"call" function_name

		field:		object
				field "." variable

		condition:	'(' cond_or ')'

		cond_or:	cond_and
				| cond_or '||' cond_and

		cond_and:	cond_part
				| cond_and '&&' cond_part

		cond_part:	'!' cond_part2
				| cond_part2

		cond_part2:	condition
				| field(int) '<' number
				| field(int) '<=' number
				| field(int) '>' number
				| field(int) '>=' number
				| field(int) '=' number
				| field(int) '!=' number
				| field(IP)  ~ ip_list
				| field(string) ~ string(regexp)

	-----------------------------------------------------------------------
	Sample request policy program

		sub request_policy {

			if (client.ip in 10.0.0.0/8) {
				no-cache
				finish
			}

			if (req.url.host ~ "cnn.no$") {
				rewrite	s/cnn.no$/vg.no/
			}

			if (req.url.path ~ "cgi-bin") {
				no-cache
			}

			if (req.useragent ~ "spider") {
				no-new-cache
			}

			if (backend.response_time > 0.8s) {
				set req.ttlfactor = 1.5
			} elseif (backend.response_time > 1.5s) {
				set req.ttlfactor = 2.0
			} elseif (backend.response_time > 2.5s) {
				set req.ttlfactor = 5.0
			}

			/*
			 * the program contains no references to
			 * maxage, s-maxage and expires, so the
			 * default handling (RFC2616) applies
			 */
		}

	-----------------------------------------------------------------------
	Sample fetch policy program

		sub backends {
			set backend.vg.ip = {...}
			set backend.ads.ip = {...}
			set backend.chat.ip = {...}
			set backend.chat.timeout = 10s
			set backend.chat.bandwidth = 2000 MB/s
			set backend.other.ip = {...}
		}

		sub vg_backend {
			set backend.ip = {10.0.0.1-5}
			set backend.timeout = 4s
			set backend.bandwidth = 2000Mb/s
		}

		sub fetch_policy {

			if (req.url.host ~ "/vg.no$/") {
				set req.backend = vg
				call vg_backend
			} else {
				/* XXX: specify 404 page url ? */
				error 404
			}

			if (backend.response_time > 2.0s) {
				if (req.url.path ~ "/landbrugspriser/") {
					error 504
				}
			}
			fetch
			if (backend.down) {
				if (obj.exist) {
					set obj.ttl += 10m
					finish
				}
				switch_config ohhshit
			}
			if (obj.result == 404) {
				error 300 "http://www.vg.no"
			}
			if (obj.result != 200) {
				finish
			}
			if (obj.size > 256k) {
				no-cache
			} else if (obj.size > 32k && obj.ttl < 2m) {
				obj.tll = 5m
			}
			if (backend.response_time > 2.0s) {
				set ttl *= 2.0
			}
		}

		sub prefetch_policy {

			if (obj.usage < 10 && obj.ttl < 5m) {
				fetch
			}
		}

	-----------------------------------------------------------------------
	Purging

	When a purge request comes in, the regexp is tagged with the next
	generation number and added to the tail of the list of purge regexps.

	Before a sender transmits an object, it is checked against any
	purge-regexps which have higher generation number than the object
	and if it matches the request is sent to a fetcher and the object
	purged.

	If there were purge regexps with higher generation to match, but
	they didn't match, the object is tagged with the current generation
	number and moved to the tail of the list.

	Otherwise, the object does not change generation number and is
	not moved on the generation list.

	New Objects are tagged with the current generation number and put
	at the tail of the list.

	Objects are removed from the generation list when deleted.

	When a purge object has a lower generation number than the first
	object on the generation list, the purge object has been completed
	and will be removed.  A log entry is written with number of compares
	and number of hits.

	-----------------------------------------------------------------------
	Random notes

		swap backed storage

		slowstart by config-flipping
			start-config has peer servers as backend
			once hitrate goes above limit, management process
			flips config to 'real' config.

		stat-object
			always URL, not regexp

		management + varnish process in one binary, comms via pipe

		Change from config with long expiry to short expiry, how
		does the ttl drop ?  (config sequence number invalidates
		all calculated/modified attributes.)

		Mgt process holds copy of acceptor socket ->  Restart without
		lost client requests.

		BW limit per client IP: create shortlived object (<4sec)
		to hold status.  Enforce limits by delaying responses.


	-----------------------------------------------------------------------
	Source structure


		libvarnish
			library with interface facilities, for instance
			functions to open&read shmem log

		varnish
			varnish sources in three classes

	-----------------------------------------------------------------------
	protocol cluster/mgt/varnish

	object_query url -> TTL, size, checksum
	{purge,invalidate} regexp
	object_status url -> object metadata

	load_config filename
	switch_config configname
	list_configs
	unload_config

	freeze 	# stop the clock, freezes the object store
	thaw

	suspend	# stop acceptor accepting new requests
	resume

	stop	# forced stop (exits) varnish process
	start
	restart = "stop;start"

	ping $utc_time -> pong $utc_time

	# cluster only
	config_contents filename $inline -> compilation messages

	stats [-mr] -> $data

	zero stats

	help

	-----------------------------------------------------------------------
	CLI (local)
		import protocol from above

		telnet localhost someport
		authentication:
			password $secret
		secret stored in {/usr/local}/etc/varnish.secret (400 root:wheel)


	-----------------------------------------------------------------------
	HTML (local)

		php/cgi-bin thttpd ?
		(alternatively direct from C-code.)
		Everything the CLI can do +
		stats
			popen("rrdtool");
		log view

	-----------------------------------------------------------------------
	CLI (cluster)
		import protocol from above, prefix machine/all
		compound stats
		accept / deny machine (?)
		curses if you set termtype

	-----------------------------------------------------------------------
	HTML (cluster)
		ditto
		ditto

		http://clustercontrol/purge?regexp=fslkdjfslkfdj
			POST with list of regexp
			authentication ? (IP access list)

	-----------------------------------------------------------------------
	Mail (cluster)

		pgp signed emails with CLI commands

	-----------------------------------------------------------------------
	connection varnish -> cluster controller

		Encryption
			SSL
		Authentication (?)
			IP number checks.

		varnish -c clusterid -C mycluster_ctrl.vg.no

	-----------------------------------------------------------------------
	Filer
		/usr/local/sbin/varnish
			contains mgt + varnish process.
			if -C argument, open SSL to cluster controller.
			Arguments:
				-p portnumber
				-c clusterid@cluster_controller
				-f config_file
				-m memory_limit
				-s kind[,storage-options]
				-l logfile,logsize
				-b backend ip...
				-d debug
				-u uid
				-a CLI_port

			KILL SIGTERM	-> suspend, stop

		/usr/local/sbin/varnish_cluster
			Cluster controller.
			Use syslog

			Arguments:
				-f config file
				-d debug
				-u uid (?)

		/usr/local/sbin/varnish_logger
			Logfile processor
			-i shmemfile
			-e regexp
			-o "/var/log/varnish.%Y%m%d.traffic"
			-e regexp2
			-n "/var/log/varnish.%Y%m%d.exception"  (NCSA format)
			-e regexp3
			-s syslog_level,syslogfacility
			-r host:port	send via TCP, prefix hostname

			SIGHUP: reopen all files.

		/usr/local/bin/varnish_cli
			Command line tool.

		/usr/local/share/varnish/etc/varnish.conf
			default request + fetch + backend scripts

		/usr/local/share/varnish/etc/rfc2616.conf
			RFC2616 compliant handling function

		/usr/local/etc/varnish.conf (optional)
			request + fetch + backend scripts

		/usr/local/share/varnish/etc/varnish.startup
			default startup sequence

		/usr/local/etc/varnish.startup (optional)
			startup sequence

		/usr/local/etc/varnish_cluster.conf
			XXX

		{/usr/local}/etc/varnish.secret
			CLI password file.

	-----------------------------------------------------------------------
	varnish.startup

		load config /foo/bar startup_conf
		switch config startup_conf
		!mypreloadscript
		load config /foo/real real_conf
		switch config real_conf
		resume

Fourth Varnish Design Note
--------------------------

You'd think we'd be cookin' with gas now, and indeed we were, but now
all the difficult details started to raise ugly questions, and it
has never stopped since::

	Questions:

	*  Which "Host:" do we put in the request to the backend ?

	      The one we got from the client ?

	      The ip/dns-name of the backend ?

	      Configurable in VCL backend declaration ?

	      (test with www.ing.dk)

	*  Construction of headers for queries to backend ?

	      How much do we take from client headers, how much do we make up ?

	      Some sites discriminate contents based on User-Agent header.
		 (test with www.krak.dk/www.rs-components.dk)

	      Cookies

	*  Mapping of headers from backend reply to the reply to client

	      Which fields come from the backend ?

	      Which fields are made up on the spot ? (expiry time ?)

	      (Static header fields can be prepended to contents in storage)


	*  3xx replies from the backend

	      Does varnish follow a redirection or do we pass it to the client ?

	      Do we cache 3xx replies ?


The first live traffic
----------------------

The final bit of history I want to share is the IRC log from the
first time tried to put real live traffic through Varnish.

The language is interscandinavian, but I think non-vikings can get
still get the drift::

	**** BEGIN LOGGING AT Thu Jul  6 12:36:48 2006

	Jul 06 12:36:48 *	Now talking on #varnish
	Jul 06 12:36:48 *	EvilDES gives channel operator status to andersb
	Jul 06 12:36:53 *	EvilDES gives channel operator status to phk
	Jul 06 12:36:53 <andersb>	hehe
	Jul 06 12:36:56 <EvilDES>	sånn
	Jul 06 12:37:00 <andersb>	Jepps, er dere klare?
	Jul 06 12:37:08 <phk>	Jeg har varnish oppe og køre med leonora som backend.
	Jul 06 12:37:12 *	EvilDES has changed the topic to: Live testing in progress!
	Jul 06 12:37:16 *	EvilDES sets mode +t #varnish
	Jul 06 12:37:19 <andersb>	Da setter jeg på trafikk
	Jul 06 12:37:36 <phk>	andersb: kan du starte med bare at give us trafiik i 10 sekunder eller så ?
	Jul 06 12:37:49 *	edward (edward@f95.linpro.no) has joined #varnish
	Jul 06 12:38:32 <andersb>	hmm, først må jeg få trafikk dit.
	Jul 06 12:38:55 <andersb>	Har noe kommet? Eller har det blitt suprt etter /systemmeldinger/h.html som er helsefilen?
	Jul 06 12:39:10 <andersb>	s/suprt/spurt/
	Jul 06 12:39:41 <EvilDES>	ser ingenting
	Jul 06 12:39:45 <phk>	jeg har ikke set noget endnu...
	Jul 06 12:40:35 <phk>	den prøver på port 80
	Jul 06 12:41:24 <andersb>	okay..
	Jul 06 12:41:31 <EvilDES>	kan vi ikke bare kjøre varnishd på port 80?
	Jul 06 12:41:46 <phk>	ok, jeg ville bare helst ikke køre som root.
	Jul 06 12:41:47 <andersb>	Prøver den noe annet nå?
	Jul 06 12:41:59 <phk>	nej stadig 80.
	Jul 06 12:42:03 <phk>	Jeg starter varnishd som root
	Jul 06 12:42:08 <EvilDES>	nei, vent
	Jul 06 12:42:08 <andersb>	Topp
	Jul 06 12:42:11 <andersb>	okay
	Jul 06 12:42:15 <andersb>	kom det 8080 nå?
	Jul 06 12:42:18 <EvilDES>	sysctl reserved_port
	Jul 06 12:43:04 <andersb>	okay? Får dere 8080 trafikk nå?
	Jul 06 12:43:08 <EvilDES>	sysctl net.inet.ip.portrange.reservedhigh=79
	Jul 06 12:44:41 <andersb>	Okay, avventer om vi skal kjøre 8080 eller 80.
	Jul 06 12:45:56 <EvilDES>	starter den på port 80 som root
	Jul 06 12:46:01 <phk>	den kører nu
	Jul 06 12:46:01 <andersb>	Okay, vi har funnet ut at måten jeg satte 8080 på i lastbalanserern var feil.
	Jul 06 12:46:07 <andersb>	okay på 80?
	Jul 06 12:46:12 <phk>	vi kører
	Jul 06 12:46:14 <EvilDES>	ja, masse trafikk
	Jul 06 12:46:29 <phk>	omtrent 100 req/sec
	Jul 06 12:46:37 <phk>	and we're dead...
	Jul 06 12:46:40 <EvilDES>	stopp!
	Jul 06 12:46:58 <andersb>	den stopper automatisk.
	Jul 06 12:47:04 <andersb>	Vi kan bare kjøre det slik.
	Jul 06 12:47:06 <EvilDES>	tok noen sekunder
	Jul 06 12:47:20 <andersb>	Npr den begynner svar på 80 så vil lastbalanserern finne den fort og sende trafikk.
	Jul 06 12:47:41 <EvilDES>	ca 1500 connection requests kom inn før den sluttet å sende oss trafikk
	Jul 06 12:47:49 <EvilDES>	altså, 1500 etter at varnishd døde
	Jul 06 12:48:02 <andersb>	tror det er en god nok måte å gjøre det på. Så slipper vi å configge hele tiden.
	Jul 06 12:48:07 <EvilDES>	greit
	Jul 06 12:48:11 <EvilDES>	det er dine lesere :)
	Jul 06 12:48:19 <andersb>	ja :)
	Jul 06 12:48:35 <andersb>	kan sette ned retry raten litt.
	Jul 06 12:49:15 <andersb>	>> AS3408-2 VG Nett - Real server 21 # retry
	Jul 06 12:49:16 <andersb>	Current number of failure retries: 4
	Jul 06 12:49:16 <andersb>	Enter new number of failure retries [1-63]: 1
	Jul 06 12:49:33 <andersb>	^^ before de decalres dead
	Jul 06 12:49:41 <andersb>	he declairs :)
	Jul 06 12:51:45 <phk>	I've saved the core, lets try again for another shot.
	Jul 06 12:52:09 <andersb>	sure :)
	Jul 06 12:52:34 <andersb>	When you start port 80 loadbalancer will send 8 req's for h.html then start gicing traficc
	Jul 06 12:53:00 <andersb>	^^ Microsoft keyboard
	Jul 06 12:53:09 <phk>	ok, jeg starter
	Jul 06 12:53:10 <EvilDES>	you need to get a Linux keyboard
	Jul 06 12:53:16 <andersb>	Yeah :)
	Jul 06 12:53:18 <EvilDES>	woo!
	Jul 06 12:53:21 <phk>	boom.
	Jul 06 12:53:25 <EvilDES>	oops
	Jul 06 12:53:35 <EvilDES>	18 connections, 77 requests
	Jul 06 12:53:40 <EvilDES>	that didn't last long...
	Jul 06 12:54:41 <andersb>	longer than me :) *rude joke
	Jul 06 12:55:04 <phk>	bewm
	Jul 06 12:55:22 <andersb>	can I follow a log?
	Jul 06 12:55:39 <andersb>	with: lt-varnishlog ?
	Jul 06 12:56:27 <phk>	samme fejl
	Jul 06 12:56:38 <phk>	andersb: jeg gemmer logfilerne
	Jul 06 12:57:00 <phk>	bewm
	Jul 06 12:57:13 <andersb>	phk: Jepp, men for min egen del for å se når dere skrur på etc. Da lærer jeg loadbalancer ting.
	Jul 06 12:57:51 <phk>	ok, samme fejl igen.
	Jul 06 12:58:02 <phk>	jeg foreslår vi holder en lille pause mens jeg debugger.
	Jul 06 12:58:09 <andersb>	sure.
	Jul 06 12:58:16 <EvilDES>	andersb: cd ~varnish/varnish/trunk/varnish-cache/bin/varnishlog
	Jul 06 12:58:21 <EvilDES>	andersb: ./varnishlog -o
	Jul 06 12:58:37 <EvilDES>	andersb: cd ~varnish/varnish/trunk/varnish-cache/bin/varnishstat
	Jul 06 12:58:43 <EvilDES>	andersb: ./varnishstat -c
	Jul 06 12:58:44 <phk>	eller ./varnislog -r _vlog3 -o | less
	Jul 06 13:00:02 <andersb>	Jeg går meg en kort tur. Straks tilbake.
	Jul 06 13:01:27 <phk>	vi kører igen
	Jul 06 13:02:31 <phk>	2k requests
	Jul 06 13:02:57 <phk>	3k
	Jul 06 13:03:39 <phk>	5k
	Jul 06 13:03:55 <EvilDES>	ser veldig bra ut
	Jul 06 13:04:06 <EvilDES>	hit rate > 93%
	Jul 06 13:04:13 <EvilDES>	95%
	Jul 06 13:05:14 <phk>	800 objects
	Jul 06 13:05:32 <EvilDES>	load 0.28
	Jul 06 13:05:37 <EvilDES>	0.22
	Jul 06 13:05:52 <EvilDES>	CPU 98.9% idle :)
	Jul 06 13:06:12 <phk>	4-5 Mbit/sec
	Jul 06 13:06:42 <andersb>	nice :)
	Jul 06 13:06:49 <andersb>	vi kjører til det krasjer?
	Jul 06 13:06:58 <phk>	jep
	Jul 06 13:07:05 <phk>	du må gerne åbne lidt mere
	Jul 06 13:07:20 <andersb>	okay
	Jul 06 13:07:41 <andersb>	3 ganger mer...
	Jul 06 13:08:04 <andersb>	si fra når dere vil ha mer.
	Jul 06 13:08:24 <phk>	vi gir den lige et par minutter på det her niveau
	Jul 06 13:09:17 <phk>	bewm
	Jul 06 13:09:31 <EvilDES>	        3351        0.00 Client connections accepted
	Jul 06 13:09:31 <EvilDES>	       23159        0.00 Client requests received
	Jul 06 13:09:31 <EvilDES>	       21505        0.00 Cache hits
	Jul 06 13:09:31 <EvilDES>	        1652        0.00 Cache misses
	Jul 06 13:10:17 <phk>	kører igen
	Jul 06 13:10:19 <EvilDES>	here we go again
	Jul 06 13:11:06 <phk>	20mbit/sec
	Jul 06 13:11:09 <phk>	100 req/sec
	Jul 06 13:12:30 <andersb>	nice :)
	Jul 06 13:12:46 <andersb>	det er gode tall, og jeg skal fortelle dere hvorfor senere
	Jul 06 13:12:49 <phk>	steady 6-8 mbit/sec
	Jul 06 13:12:52 <andersb>	okay.
	Jul 06 13:13:00 <phk>	ca 50 req/sec
	Jul 06 13:13:04 <EvilDES>	skal vi øke?
	Jul 06 13:13:14 <phk>	ja, giv den det dobbelte hvis du kan
	Jul 06 13:13:19 <andersb>	vi startet med 1 -> 3 -> ?
	Jul 06 13:13:22 <phk>	6
	Jul 06 13:13:23 <andersb>	6
	Jul 06 13:13:34 <andersb>	done
	Jul 06 13:13:42 <andersb>	den hopper opp graceful.
	Jul 06 13:13:54 <EvilDES>	boom
	Jul 06 13:14:06 <andersb>	:)
	Jul 06 13:14:11 <EvilDES>	men ingen ytelsesproblemer
	Jul 06 13:14:19 <EvilDES>	bare bugs i requestparsering
	Jul 06 13:14:20 <phk>	kører igen
	Jul 06 13:14:26 <phk>	bewm
	Jul 06 13:14:31 <phk>	ok, vi pauser lige...
	Jul 06 13:17:40 <phk>	jeg har et problem med "pass" requests, det skal jeg lige have fundet inden vi går videre.
	Jul 06 13:18:51 <andersb>	Sure.
	Jul 06 13:28:50 <phk>	ok, vi prøver igen
	Jul 06 13:29:09 <phk>	bewm
	Jul 06 13:29:35 <phk>	more debugging
	Jul 06 13:33:56 <phk>	OK, found the/one pass-mode bug
	Jul 06 13:33:58 <phk>	trying again
	Jul 06 13:35:23 <phk>	150 req/s 24mbit/s, still alive
	Jul 06 13:37:02 <EvilDES>	andersb: tror du du klarer å komme deg hit til foredraget, eller er du helt ødelagt?
	Jul 06 13:37:06 <phk>	andersb: giv den 50% mere trafik
	Jul 06 13:39:46 <andersb>	mer trafikk
	Jul 06 13:39:56 <andersb>	EvilDES: Nei :(( Men Stein fra VG Nett kommer.
	Jul 06 13:41:25 <EvilDES>	btw, har du noen data om hva load balanceren synes om varnish?
	Jul 06 13:41:50 <EvilDES>	jeg regner med at den følger med litt på hvor god jobb vi gjør
	Jul 06 13:43:10 <phk>	Jeg genstarter lige med flere workerthreads...
	Jul 06 13:43:43 <phk>	jeg tror 20 workerthreads var for lidt nu...
	Jul 06 13:43:47 <phk>	nu har den 220
	Jul 06 13:44:40 <EvilDES>	        2976      107.89 Client connections accepted
	Jul 06 13:44:41 <EvilDES>	       10748      409.57 Client requests received
	Jul 06 13:44:41 <EvilDES>	        9915      389.59 Cache hits
	Jul 06 13:45:13 <EvilDES>	det var altså 400 i sekundet :)
	Jul 06 13:45:45 <phk>	og ingen indlysende fejl på www.vg.no siden :-)
	Jul 06 13:45:54 <phk>	bewm
	Jul 06 13:47:16 <EvilDES>	andersb: hvor stor andel av trafikken hadde vi nå?
	Jul 06 13:48:06 <EvilDES>	altså, vekt i load balanceren i forhold til totalen
	Jul 06 13:49:20 <phk>	ok, kun 120 threads så...
	Jul 06 13:50:48 <andersb>	9
	Jul 06 13:52:45 <phk>	andersb: 9 -> 12 ?
	Jul 06 13:52:48 <EvilDES>	andersb: 9 til varnish, men hvor mye er den totale vekten?
	Jul 06 13:52:58 <EvilDES>	har vi 1%? 5%? 10%?
	Jul 06 13:54:37 <EvilDES>	nå passerte vi nettopp 50000 requests uten kræsj
	Jul 06 13:55:36 <phk>	maskinen laver ingenting...  98.5% idle
	Jul 06 13:56:21 <andersb>	12 maskiner med weight 20
	Jul 06 13:56:26 <andersb>	1 med weight 40
	Jul 06 13:56:29 <andersb>	varnish med 9
	Jul 06 13:57:01 <andersb>	si fra når dere vil ha mer trafikk.
	Jul 06 13:57:02 <phk>	9/289 = 3.1%
	Jul 06 13:57:12 <phk>	andersb: giv den 15
	Jul 06 13:57:44 <andersb>	gjort
	Jul 06 13:59:43 <andersb>	dette er morro. Jeg må si det.
	Jul 06 14:00:27 <phk>	20-23 Mbit/sec steady, 200 req/sec, 92.9% idle
	Jul 06 14:00:30 <phk>	bewm
	Jul 06 14:00:46 <EvilDES>	OK
	Jul 06 14:00:57 <EvilDES>	jeg tror vi kan slå fast at ytelsen er som den skal være
	Jul 06 14:01:33 <EvilDES>	det er en del bugs, men de bør det gå an å fikse.
	Jul 06 14:01:34 <andersb>	Jepp :) Det så pent ut...
	Jul 06 14:01:53 <phk>	jeg tror ikke vi har set skyggen af hvad Varnish kan yde endnu...
	Jul 06 14:01:53 <EvilDES>	andersb: hvordan ligger vi an i forhold til Squid?
	Jul 06 14:01:58 <andersb>	pent :)
	Jul 06 14:02:13 <andersb>	Jeg har ikke fått SNMP opp på dene boksen, jeg burde grafe det...
	Jul 06 14:02:23 <EvilDES>	snmp kjører på c21
	Jul 06 14:02:33 <EvilDES>	tror agero satte det opp
	Jul 06 14:02:36 <EvilDES>	aagero
	Jul 06 14:02:38 <andersb>	Ja, men jeg har ikke mal i cacti for bsnmpd
	Jul 06 14:02:43 <EvilDES>	ah, ok
	Jul 06 14:03:03 <EvilDES>	men den burde støtte standard v2 mib?
	Jul 06 14:03:26 <andersb>	det er ikke protocoll feil :)
	Jul 06 14:03:42 <andersb>	Hva er byte hitratio forresetn?
	Jul 06 14:03:52 <EvilDES>	det tror jeg ikke vi måler
	Jul 06 14:03:55 <EvilDES>	enda
	Jul 06 14:03:59 <phk>	andersb: den har jeg ikke stats på endnu.
	Jul 06 14:04:22 <phk>	ok, forrige crash ligner en 4k+ HTTP header...
	Jul 06 14:04:27 <phk>	(eller en kodefejl)
	Jul 06 14:06:03 <phk>	andersb: prøv at øge vores andel til 20
	Jul 06 14:06:26 <EvilDES>	hvilken vekt har hver av de andre cachene?
	Jul 06 14:06:49 <phk>	20 og en med 40
	Jul 06 14:07:50 <andersb>	gjort
	Jul 06 14:08:59 <phk>	440 req/s 43mbit/s
	Jul 06 14:09:17 <phk>	bewm
	Jul 06 14:09:18 <EvilDES>	bewm
	Jul 06 14:10:30 <EvilDES>	oj
	Jul 06 14:10:39 <EvilDES>	vi var oppe over 800 req/s et øyeblikk
	Jul 06 14:10:46 <phk>	60mbit/sec
	Jul 06 14:10:52 <phk>	og 90% idle :-)
	Jul 06 14:10:59 <EvilDES>	ingen swapping
	Jul 06 14:11:58 <EvilDES>	og vi bruker nesten ikke noe minne - 3 GB ledig fysisk RAM
	Jul 06 14:13:02 <phk>	ca 60 syscall / req
	Jul 06 14:14:31 <andersb>	nice :)
	Jul 06 14:14:58 <phk>	andersb: prøv at give os 40
	Jul 06 14:17:26 <andersb>	gjort
	Jul 06 14:18:17 <phk>	det ligner at trafikken falder her sidst på eftermiddagen...
	Jul 06 14:19:07 <andersb>	ja :)
	Jul 06 14:19:43 <phk>	andersb: så skal vi nok ikke øge mere, nu nærmer vi os hvad 100Mbit ethernet kan klare.
	Jul 06 14:19:58 <andersb>	bra :)
	Jul 06 14:20:36 <phk>	42mbit/s steady
	Jul 06 14:20:59 <EvilDES>	40 av 320?
	Jul 06 14:21:06 <EvilDES>	12,5%
	Jul 06 14:21:43 *	nicholas (nicholas@nfsd.linpro.no) has joined #varnish
	Jul 06 14:22:00 <phk>	det der cluster-noget bliver der da ikke brug for når vi har 87% idle
	Jul 06 14:23:05 <andersb>	hehe :)
	Jul 06 14:24:38 <andersb>	skal stille de andre ned litt for 48 er max
	Jul 06 14:24:57 <phk>	jeg tror ikke vi skal gå højere før vi har gigE
	Jul 06 14:25:14 <andersb>	4-5MB/s
	Jul 06 14:25:32 <andersb>	lastbalanserer backer off på 100 Mbit
	Jul 06 14:25:35 <andersb>	:)
	Jul 06 14:25:42 <andersb>	Så vi kan kjøre nesten til taket.
	Jul 06 14:26:01 <andersb>	hvis det har noe poeng.
	Jul 06 14:26:09 <andersb>	crash :)
	Jul 06 14:27:33 <phk>	bewm
	Jul 06 14:29:08 <andersb>	Stilt inn alle på weight 5
	Jul 06 14:29:17 <andersb>	bortsett fra 1 som er 10
	Jul 06 14:29:20 <andersb>	varnish er 5
	Jul 06 14:29:24 <phk>	så giv os 20
	Jul 06 14:29:51 <andersb>	gjort
	Jul 06 14:30:58 <phk>	vi får kun 300 req/s
	Jul 06 14:31:04 <phk>	Ahh der skete noget.
	Jul 06 14:32:41 <phk>	ok, ved denne last bliver backend connections et problem, jeg har set dns fejl og connection refused
	Jul 06 14:33:10 <phk>	dns fejl
	Jul 06 14:33:21 <andersb>	okay, pek den mot 10.0.2.5
	Jul 06 14:33:28 <andersb>	det er layer 2 squid cache
	Jul 06 14:33:35 <andersb>	morro å teste det og.
	Jul 06 14:33:54 <phk>	det gør jeg næste gang den falder
	Jul 06 14:34:48 <phk>	jeg kunne jo også bare give leonors IP# istedet... men nu kører vi imod squid
	Jul 06 14:36:05 <andersb>	ja, gi leonora IP det er sikkert bedre. Eller det kan jo være fint å teste mot squid og :)
	Jul 06 14:39:04 <phk>	nu kører vi med leonora's IP#
	Jul 06 14:39:33 <phk>	nu kører vi med leonora's *rigtige* IP#
	Jul 06 14:41:20 <phk>	Nu er vi færdige med det her 100Mbit/s ethernet, kan vi få et til ?  :-)
	Jul 06 14:41:42 <andersb>	lol :)
	Jul 06 14:42:00 <andersb>	For å si det slik. Det tar ikke mange dagene før Gig switch er bestilt :)
	Jul 06 14:43:05 <phk>	bewm
	Jul 06 14:43:13 <phk>	ok, jeg synes vi skal stoppe her.
	Jul 06 14:43:41 <EvilDES>	jepp, foredrag om 15 min
	Jul 06 14:43:57 <andersb>	jepp
	Jul 06 14:44:23 <andersb>	disabled server
	Jul 06 14:45:29 <EvilDES>	dette har vært en veldig bra dag.
	Jul 06 14:45:49 <EvilDES>	hva skal vi finne på i morgen? skifte ut hele Squid-riggen med en enkelt Varnish-boks? ;)
	Jul 06 14:45:53 <andersb>	lol
	Jul 06 14:46:15 *	EvilDES må begynne å sette i stand til foredraget
	Jul 06 14:46:17 <andersb>	da må jeg har Gig switch. Eller så kan være bære med en HP maskin å koble rett på lastbal :)
	Jul 06 14:46:22 <phk>	kan vi ikke nøjes med en halv varnish box ?
	Jul 06 14:46:41 <EvilDES>	vi må ha begge halvdeler for failover
	Jul 06 14:47:01 <andersb>	:)
	Jul 06 14:47:14 <andersb>	kan faile tilbake til de andre.
	Jul 06 14:47:25 <andersb>	Jeg klarer ikke holde meg hjemme.
	Jul 06 14:47:33 <andersb>	 Jeg kommer oppover om litt :)
	Jul 06 14:47:39 <andersb>	Ringer på.
	Jul 06 14:48:19 <andersb>	må gå en tur nå :)
	Jul 06 14:48:29 *	andersb has quit (BitchX: no additives or preservatives)
	Jul 06 14:49:44 <EvilDES>	http://www.des.no/varnish/
	**** ENDING LOGGING AT Thu Jul  6 14:52:04 2006

*phk*

