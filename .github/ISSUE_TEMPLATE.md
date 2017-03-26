<!--- Things to check before you report a bug

    - Is the issue you are seeing clearly an issue in varnish core or
      could it be a support question? We do not use github issues for
      support questions, please refer to
      http://varnish-cache.org/support/index.html when in doubt.

    - For panics (varnish crashes), bug reports are most useful if

      - you are running recent code
	- ideally master
	- but at least the latest release of a supported version

      - you got
	- debuginfo packages installed when running binaries from
	  packages if these are available from the package source
	  you are using

	- or have compiled with debug information whenever possible
	  (configure --enable-debugging-symbols)

    If you have considered these recommendations, please go ahead and
    follow this template
-->
<!--- Provide a general summary of the issue in the Title above -->

## Expected Behavior
<!--- Did you check that there are no similar bug reports or pull requests? -->
<!---
    If your panic happens in the child_sigsegv_handler function, look at the
    backtrace to determine whether it is similar to another issue. When in
    doubt, open a new one and it will be closed as a duplicate if needed.
-->
<!--- If you're describing a bug, tell us what should happen -->
<!--- If you're suggesting a change/improvement, tell us how it should work -->
<!---
    If it's a packaging bug (including sysv or systemd services bugs) please
    open an issue on varnishcache/pkg-varnish-cache instead.
-->
<!---
    If it's a feature request, please start a thread on the misc list instead.
    https://www.varnish-cache.org/lists/mailman/listinfo/varnish-misc
-->

## Current Behavior
<!--- If describing a bug, tell us what happens instead of the expected behavior -->
<!--- If suggesting a change/improvement, explain the difference from current behavior -->

## Possible Solution
<!--- Not obligatory, but suggest a fix/reason for the bug, -->
<!--- or ideas how to implement the addition or change -->

## Steps to Reproduce (for bugs)
<!--- Provide a link to a live example, or an unambiguous set of steps to -->
<!--- reproduce this bug. Include code to reproduce, if relevant -->
1.
2.
3.
4.

## Context
<!--- How has this issue affected you? What are you trying to accomplish? -->
<!--- Providing context helps us come up with a solution that is most useful in the real world -->

## Your Environment
<!--- Include as many relevant details about the environment you experienced the bug in -->
* Version used:
* Operating System and version:
* Source of binary packages used (if any)
