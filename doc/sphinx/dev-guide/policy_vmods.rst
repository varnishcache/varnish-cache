..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _policy-vmods:

Bundling VMODs with the Varnish distribution
--------------------------------------------

Decisions about whether to add a new Varnish module (VMOD) to those
bundled with Varnish are guided by these criteria.

* The VMOD is known to be in widespread use and in high demand for
  common use cases.

* Or, if the VMOD is relatively new, it provides compelling features
  that the developer group agrees will be a valuable enhancement for
  the project.

* The VMOD does not create dependencies on additional external
  libraries. VMODs that are "glue" for a library come from third
  parties.

  * We don't want to add new burdens of dependency and compatibility
    to the project.

  * We don't want to force Varnish deployments to install more than
    admins explicitly choose to install.

* The VMOD code follows project conventions (passes make distcheck,
  follows source code style, and so forth).

  * A pull request can demonstrate that this is the case (after any
    necessary fixups).

* The developer group commits to maintaining the code for the long run
  (so there will have to be a consensus that we're comfortable with
  it).
