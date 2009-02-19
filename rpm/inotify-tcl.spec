Name:           inotify-tcl
Version:        1.0.0
Release:        1
Source:         inotify-tcl-1.0.0.tar.gz
License:        GPL
Vendor:         Codeforge (Pty) Ltd.
Group:          Applications/System
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Requires:       tcl, itcl
Summary:        Tcl extension providing access to the inotify facility

%description
This package allows Tcl scripts to listen for events on directories

%prep
%setup -q

%build

%install
rm -rf $RPM_BUILD_ROOT
./configure --with-tcl=/usr/lib
make DESTDIR=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%files
/usr/lib/inotify
/usr/share/man/mann/inotify.n.gz

%post
# No, I can't believe it either
test -e /usr/lib/itcl3.3 || ln -s /usr/share/tcl/itcl3.3 /usr/lib

%changelog
* Fri Aug 08 2008 Cyan Ogilvie <cyan.ogilvie@gmail.com> 1.0.0-1
- Build fixes

* Mon Aug 27 2007 Cyan Ogilvie <cyan.ogilvie@gmail.com> 0.1.0-1
- Initial version 

