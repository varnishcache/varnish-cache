.. _users-guide-command-line:

Typical command line options
----------------------------

On a modern Linux distro the various options that are used when
starting up Varnish are stored in /etc/default/varnish (Debian, Ubuntu) or
/etc/sysconfig/varnish (Red Hat, Centos).

There are quite a few options you can tweak but most of you will only
need to change a few them.

The typical command line options you want to change are:

-a *listen_address*
    What address should Varnish listen to. The default is to listen to
    all IP adresses and stick to port 80. ":80" will ask Varnish to
    listen to all adresses, both IPv4 and IPv6 and is probably a
    sensible thing.
 
-f *config file*
     The -f options specifies what VCL file Varnish should use as the default.

-s *storage options*

     This is probably the most important one. The default is to use
     the memory storage backend and to allocate a small amount of
     memory. On a small site this might suffice. If you have dedicated
     Varnish Cache server you most definitivly want to increase
     the memory allocated or consider another backend. 
     Please note that in addition to the memory allocated by the
     storage engine itself Varnish also has internal data structures
     that consume memory. More or less 1kb per object.  
     See also :ref:`guide-storage`.

-T *listen address*  
     Varnish has a built-in text-based administration
     interface. Activating the interface makes Varnish manageble
     without stopping it. You can specify what interface the
     management interface should listen to. Make sure you don't expose
     the management interface to the world as you can easily gain root
     access to a system via the Varnish management interface. I
     recommend tieing it to localhost. If you have users on your
     system that you don't fully trust, use firewall rules to restrict
     access to the interface to root only.

For a complete list of the command line parameters please see
:ref:`ref-varnishd-options`.

