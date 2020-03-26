Multiarch building, testing & packaging
=======================================

Varnish Cache uses CircleCI_ for building, testing and creating packages for
several Linux distributions for both x86_64 and aarch64 architectures.

Since CircleCI provides only x86_64 VMs the setup uses Docker and QEMU to be
able to build, test and create packages for aarch64.  This is accomplished by
registering ``qemu-user-static`` for the CircleCI ``machine`` executor::

    sudo docker run --rm --privileged multiarch/qemu-user-static --reset --credential yes --persistent yes

Note 1: **--credential yes** is needed so that *setuid* flag is working.
Without it ``sudo`` does not work in the Docker containers with architecture
different than x86_64.

Note 2: **--persistent yes** is needed so that there is no need to use
``:register`` tag. This way one can run locally pure foreign arch Docker
images, like the official ``arm64v8/***`` ones.

With QEMU registered each build step can start a Docker image for any of the
supported architectures to execute the ``configure``, ``make``, package steps.

Workflows
---------

There are two CircleCI workflows:

commit
~~~~~~

It is executed after each push to any branch, including Pull Requests

The ``commit`` workflow runs two jobs:

- ``dist`` - this job creates the source code distribution of Varnish Cache as
  compressed archive (``varnish-${VERSION}.tar.gz``).

- ``distcheck`` - untars the source code distribution from ``dist`` job and
  builds (*configure*, *make*) on different Linux distributions

nightly
~~~~~~~

It is executed once per day at 04:00 AM UTC time.

This workflow also builds binary packages for different Linux distributions
and CPU architectures (x86_64 & aarch64) and for this reason its run takes
longer.

It runs the following jobs:

- The first two jobs that run in parallel are:

  - ``tar_pkg_tools`` - this step checks out pkg-varnish-cache_ with the
    packaging descriptions for Debian, RedHat and Alpine, and stores them in
    the build workspace for the next steps in the pipeline.

  - ``dist`` - this step creates the source code distribution of Varnish Cache
    as compressed archive (``varnish-${VERSION}.tar.gz``). This archive is
    also stored in the build workspace and used later by the packaging steps.


- The next job in the workflow is ``package`` - a job  that creates the
  packages (e.g. .rpm, .deb) for each supported CPU architecture, Linux
  distribution and its major version (e.g. *x64_centos_7*,
  *aarch64_ubuntu_bionic*, *x64_alpine_3*, etc.). This step creates a
  Dockerfile on the fly by using a base Docker image. This custom Docker image
  executes a Shell script that has the recipe for creating the package for the
  specific Linux flavor, e.g.  *make-rpm-packages.sh*. The step stores the
  packages in the build workspace.

- Finally, if the previous jobs are successful, a final step is executed -
  ``collect_packages``. This step creates an archive with all packages and
  stores it as an artifact that can be uploaded to PackageCloud_.


More
----

This setup can be easily extended for any CPU architectures supported by QEMU
and for any Linux distributions which have Docker image. To do this one needs
to add a new ``package`` job with the proper parameters for it.

At the moment the setup uses *raw* Docker images and installs the required
Linux distribution dependencies before running the tests/build/packaging code.
This could be optimized to save some execution time by creating custom Docker
images that extend the current ones and pre-installs the required
dependencies.

.. _CircleCI: https://app.circleci.com/pipelines/github/varnishcache/varnish-cache
.. _pkg-varnish-cache: https://github.com/varnishcache/pkg-varnish-cache
.. _PackageCloud: https://packagecloud.io/varnishcache/
