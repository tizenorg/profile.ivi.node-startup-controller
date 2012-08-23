Summary:	GENIVI Node Startup Controller
Name:		node-startup-controller
Version:	1
Release:	1
License:	MPLv2
Group:		System/Automotive
Source:		%{name}-%{version}.tar.bz2
BuildRequires:	glib2-devel
BuildRequires:	systemd-devel
BuildRequires:	automotive-dlt-devel
BuildRequires:	python-xml

Patch1: 0001-fix-build-and-install-for-tizen.patch

%description
GENIVI Node Starup Controller

%package dummy
Summary:	GENIVI Node Startup Controller
Requires:	%{name} = %{version}-%{release}

%description dummy
Dummy Node Startup Controller instance

%package tests
Summary:	GENIVI Node Startup Controller test scripts
Requires:	%{name} = %{version}-%{release}
Requires:      dbus-python  
Requires:      pygobject  
Requires:      python-xml

%description tests
Node Startup Controller test scripts

%prep
%setup -q
%patch1 -p1

%build

(test -d m4 || mkdir m4) && autoreconf -ivf

./configure --prefix=/usr \
	    --sysconfdir=/etc \
	    --enable-debug=no \
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
%{_sysconfdir}/dbus-1/system.d/org.genivi.NodeStartupController1.conf
%{_libdir}/node-startup-controller-1/legacy-app-handler
%{_libdir}/node-startup-controller-1/node-startup-controller
%{_libdir}/systemd/system/node-startup-controller.service
%{_datadir}/dbus-1/system-services/org.genivi.NodeStartupController1.service

%files dummy
%defattr(-,root,root,-)
%{_libdir}/node-startup-controller-1/nsm-dummy
%{_libdir}/systemd/system/nsm-dummy.service
%{_sysconfdir}/dbus-1/system.d/org.genivi.NodeStateManager.conf
%{_datadir}/dbus-1/system-services/org.genivi.NodeStateManager.Consumer.service
%{_datadir}/dbus-1/system-services/org.genivi.NodeStateManager.LifecycleControl.service

%files tests
%defattr(-,root,root,-)
%{_bindir}/gvariant-writer
%{_bindir}/test-luc-handler
%{_bindir}/legacy-app-handler-test
%{_sysconfdir}/dbus-1/system.d/legacy-app-handler-test.conf
%{_datadir}/dbus-1/system-services/legacy-app-handler-test1.service
%{_datadir}/dbus-1/system-services/legacy-app-handler-test2.service
%{_datadir}/dbus-1/system-services/legacy-app-handler-test3.service
