Summary: Varnish is a high-performance HTTP accelerator.
Name: varnish
Version: 1.0.2
Release: 6
License: BSD-like
Group: System Environment/Daemons
URL: http://www.varnish-cache.org/
Packager: Ingvar Hagelund <ingvar@linpro.no>
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: gcc gcc-c++ ncurses-devel libtool autoconf automake
Requires: gcc ncurses kernel >= 2.6.0

%description
This is the Varnish high-performance HTTP accelerator.
Documentation and additional information about Varnish is available on
the following web sites:
  http://www.varnish-cache.org/         Official web site
  http://varnish.projects.linpro.no/    Developer site and wiki

Technical questions about Varnish and this release should be addressed
to <varnish-dev@projects.linpro.no>.

Questions about commercial support and services related to Varnish
should be addressed to <varnish@linpro.no>.

Copyright (c) 2006 Verdens Gang AS
Copyright (c) 2006 Linpro AS
All rights reserved.
Author: Poul-Henning Kamp <phk@phk.freebsd.dk>

%prep
%setup -q

%build
rm -rf $RPM_BUILD_ROOT

./autogen.sh
%configure --sbindir=/usr/sbin
%{__make}

sed -e ' s/8080/80/g ' etc/vcl.conf > redhat/vcl.conf

%install
%{makeinstall}
mkdir -p %{buildroot}%{_sbindir}
mkdir -p %{buildroot}%{_docdir}/%{name}-%{version}
mkdir -p %{buildroot}/etc/varnish
mkdir -p %{buildroot}/etc/init.d
mkdir -p %{buildroot}/etc/sysconfig
mkdir -p %{buildroot}/var/lib/varnish

%{__install} -m 0644 INSTALL %{buildroot}%{_docdir}/%{name}-%{version}/INSTALL
%{__install} -m 0644 LICENSE %{buildroot}%{_docdir}/%{name}-%{version}/LICENSE
%{__install} -m 0644 README %{buildroot}%{_docdir}/%{name}-%{version}/README
%{__install} -m 0644 ChangeLog %{buildroot}%{_docdir}/%{name}-%{version}/ChangeLog
%{__install} -m 0644 redhat/README.redhat %{buildroot}%{_docdir}/%{name}-%{version}/README.redhat
%{__install} -m 0644 redhat/vcl.conf %{buildroot}%{_docdir}/%{name}-%{version}/vcl.example.conf
%{__install} -m 0644 redhat/vcl.conf %{buildroot}/etc/varnish/vcl.conf
%{__install} -m 0644 redhat/varnish.sysconfig %{buildroot}/etc/sysconfig/varnish
%{__install} -m 0755 redhat/varnish.initrc %{buildroot}/etc/init.d/varnish

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_sbindir}/varnishd
%{_bindir}/varnishhist
%{_bindir}/varnishlog
%{_bindir}/varnishncsa
%{_bindir}/varnishstat
%{_bindir}/varnishtop
%{_libdir}/libvarnish.a
%{_libdir}/libvarnish.la
%{_libdir}/libvarnish.so.0.0.0
%{_libdir}/libvarnish.so.0
%{_libdir}/libvarnishapi.a
%{_libdir}/libvarnishapi.la
%{_libdir}/libvarnishapi.so.0.0.0
%{_libdir}/libvarnishapi.so.0
%{_libdir}/libvcl.a
%{_libdir}/libvcl.la
%{_libdir}/libvcl.so.0.0.0
%{_libdir}/libvcl.so.0
%{_var}/lib/varnish
%{_mandir}/man1/varnishd.1.gz
%{_mandir}/man1/varnishhist.1.gz
%{_mandir}/man1/varnishlog.1.gz
%{_mandir}/man1/varnishncsa.1.gz
%{_mandir}/man1/varnishstat.1.gz
%{_mandir}/man1/varnishtop.1.gz
%{_mandir}/man7/vcl.7.gz

%doc %{_docdir}/%{name}-%{version}/INSTALL
%doc %{_docdir}/%{name}-%{version}/LICENSE
%doc %{_docdir}/%{name}-%{version}/README
%doc %{_docdir}/%{name}-%{version}/README.redhat
%doc %{_docdir}/%{name}-%{version}/ChangeLog
%doc %{_docdir}/%{name}-%{version}/vcl.example.conf
%config(noreplace) /etc/varnish/vcl.conf
%config /etc/init.d/varnish
%config /etc/sysconfig/varnish

%post
/sbin/chkconfig --add varnish
/sbin/chkconfig --list varnish

%changelog
* Thu Oct 19 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.02-1
- Added redhat subdir to svn
- Removed default vcl config file. Used the new upstream variant instead.
- Based build on svn. Running autogen.sh as start of build. Also added
  libtool, autoconf and automake to BuildRequires.
- Removed rule to move varnishd to sbin. This is now fixed in upstream
- Changed the sysconfig script to include a lot more nice features.
  Most of these were ripped from the Debian package. Updated initscript
  to reflect this.
* Tue Oct 10 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.01-3
- Moved Red Hat specific files to its own subdirectory
* Tue Sep 26 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.1-2
- Added gcc requirement.
- Changed to an even simpler example vcl in to /etc/varnish (thanks, perbu)
- Added a sysconfig entry
* Fri Sep 22 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.1-1
- Initial build.
