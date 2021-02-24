..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _ref-shell_tricks:

%%%%%%%%%%%%
Shell Tricks 
%%%%%%%%%%%%

All the varnish programs can be invoked with the single
argument ``--optstring`` to request their `getopt()`
specification, which simplifies wrapper scripts:

.. code-block:: text

    optstring=$(varnishfoo --optstring)

    while getopts "$optstring" opt
    do
        case $opt in
        n)
            # handle $OPTARG
            ;;
        # handle other options
        *)
            # ignore unneeded options
            ;;
        esac
    done

    varnishfoo "$@"

    # do something with the options
