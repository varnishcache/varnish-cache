Multiarch building, testing & packaging
=======================================

Varnish Cache uses CircleCI_ for building, testing and creating packages for several Linux distributions for both x86_64 and aarch64 architectures.

Since CircleCI provides only x86_64 VMs the setup uses Docker and QEMU to be able to build, test and create packages for aarch64.
This is accomplished by registering `qemu-user-static` for the CircleCI `machine` executor:

        ``sudo docker run --rm --privileged multiarch/qemu-user-static:register --reset --credential yes``

Note: **--credential yes** is needed so that *setuid* flag is working. Without it `sudo` does not work in the Docker containers with architecture different than x86_64.

With QEMU registered each build step can start a Docker image for any of the supported architectures to execute the `configure`, `make`, package steps.

Pipeline steps
-----------

1. The first two steps that run in parallel are:

    1.1. ``tar_pkg_tools`` - this step checks out pkg-varnish-cache_ with the packaging descriptions for Debian, RedHat and Alpine, and stores them in the build workspace for the next steps in the pipeline. Additionally the result files are stored as artefacts in case they are needed for debugging. 

    1.2. ``dist`` - this step creates the source code distribution of Varnish Cache as compressed archive (varnish-cache-x.y.z.tar.gz). This archive is also stored in the build workspace and used later by the packaging steps. Again the archive is stored as an artefact for debugging.


2. The next steps in the pipeline (again running in parallel) are:

    2.1. ``distcheck`` - untars the source code distribution and builds (*configure*, *make*) it for the different CPU architectures

    2.2. ``ARCH_DISTRO_RELEASE`` - step that creates the packages (e.g. .rpm, .deb) for each supported CPU architecture, Linux distribution and its major version (e.g. *x64_centos_7*, *aarch64_ubuntu_bionic*, *x64_alpine_3*, etc.). This step creates a Dockerfile on the fly by using a base Docker image. This custom Docker image executes a Shell script that has the recipe for creating the package for the specific Linux flavor, e.g. *make-rpm-packages.sh*. The step stores the packages in the build workspace and as an artefact.

3. Finally, if the previous steps are successful, a final step is executed - ``collect_packages``. This step creates an archive with all packages and stores it as an artefact that can be uploaded to PackageCloud_.


More
-------------

- This setup can be easily extended for any CPU architectures supported by QEMU and for any Linux distributions which have Docker image. To do this one needs to add a ``ARCH_DISTRO_RELEASE`` step.
- At the moment the setup uses *raw* Docker images and installs the required Linux distribution dependencies before running the tests/build/packaging code. This could be optimized to save some execution time by creating custom Docker images that extend the current ones and pre-installs the required dependencies.


.. _CircleCI: https://app.circleci.com/pipelines/github/varnishcache/varnish-cache
.. _pkg-varnish-cache: https://github.com/varnishcache/pkg-varnish-cache
.. _PackageCloud: https://packagecloud.io/varnishcache/
