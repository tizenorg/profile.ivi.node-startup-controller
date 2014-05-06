Summary:	GENIVI Node Startup Controller
Name:		node-startup-controller
Version:	1.0.2
Release:	1
License:	MPL-2.0
Group:		Automotive/GENIVI
Source:		%{name}-%{version}.tar.bz2
BuildRequires:  gtk-doc
BuildRequires:	pkgconfig(glib-2.0)
BuildRequires:	pkgconfig(libsystemd-daemon)
BuildRequires:	pkgconfig(automotive-dlt)
BuildRequires:  pkgconfig(zlib)
BuildRequires:	python-xml

%description
The Node Startup Controller (NSC) is a system lifecycle package for GENIVI
to handle some startup and shutdown functionality.

%package dummy
Summary:	GENIVI Node Startup Controller
Requires:	%{name} = %{version}-%{release}

%description dummy
Dummy Node Startup Controller instance

%prep
%setup -q

%build

(test -d m4 || mkdir m4) && gtkdocize && autoreconf -ivf

./configure --prefix=/usr \
	    --sysconfdir=/etc \
	    --enable-debug=no \
	    --enable-gtk-doc=no \
	    GDBUS_CODEGEN=`which gdbus-codegen`

%ifarch %{arm}
make ARCH=arm
%else
make ARCH=i586
%endif

%install
%make_install

%files
%defattr(-,root,root,-)
%config %{_sysconfdir}/dbus-1/system.d/org.genivi.NodeStartupController1.conf
%{_libdir}/node-startup-controller-1/legacy-app-handler
%{_libdir}/node-startup-controller-1/node-startup-controller
%{_libdir}/systemd/system/node-startup-controller.service
%{_datadir}/dbus-1/system-services/org.genivi.NodeStartupController1.service

%files dummy
%defattr(-,root,root,-)
%{_libdir}/node-startup-controller-1/nsm-dummy
%{_libdir}/systemd/system/nsm-dummy.service
%config %{_sysconfdir}/dbus-1/system.d/org.genivi.NodeStateManager.conf
%{_datadir}/dbus-1/system-services/org.genivi.NodeStateManager.Consumer.service
%{_datadir}/dbus-1/system-services/org.genivi.NodeStateManager.LifecycleControl.service
