
%global _hardened_build 1

%if 0%{?fedora} >= 15 || 0%{?rhel} >= 7
%global with_nfsidmap 1
%else
%global with_nfsidmap 0
%endif

%if ( 0%{?fedora} >= 18 || 0%{?rhel} >= 7 )
%global with_systemd 1
%else
%global with_systemd 0
%endif

%if ( 0%{?fedora} || 0%{?rhel} >= 7 )
%global with_firewalld 1
%else
%global with_firewalld 0
%endif

# Conditionally enable some FSALs, disable others.
#
# 1. rpmbuild accepts these options (gpfs as example):
#    --with gpfs
#    --without gpfs

%define on_off_switch() %%{?with_%1:ON}%%{!?with_%1:OFF}

# A few explanation about %%bcond_with and %%bcond_without
# /!\ be careful: this syntax can be quite messy
# %%bcond_with means you add a "--with" option, default = without this feature
# %%bcond_without adds a"--without" so the feature is enabled by default

%bcond_with vfs
%global use_fsal_vfs %{on_off_switch vfs}

%bcond_with nullfs
%global use_fsal_null %{on_off_switch nullfs}

%bcond_with gpfs
%global use_fsal_gpfs %{on_off_switch gpfs}

%bcond_with xfs
%global use_fsal_xfs %{on_off_switch xfs}

%bcond_with 9P
%global use_fsal_9P %{on_off_switch 9P}

%bcond_with ceph
%global use_fsal_ceph %{on_off_switch ceph}

%bcond_with proxy
%global use_fsal_proxy %{on_off_switch proxy}

%bcond_with rgw
%global use_fsal_rgw %{on_off_switch rgw}

%bcond_without gluster
%global use_fsal_gluster %{on_off_switch gluster}

%bcond_with panfs
%global use_fsal_panfs %{on_off_switch panfs}

%bcond_with rdma
%global use_rdma %{on_off_switch rdma}

%bcond_with jemalloc
%global use_jemalloc %{on_off_switch jemalloc}

%bcond_with lttng
%global use_lttng %{on_off_switch lttng}

%bcond_with utils
%global use_utils %{on_off_switch utils}

%bcond_with gui_utils
%global use_gui_utils %{on_off_switch gui_utils}

%bcond_without system_ntirpc
%global use_system_ntirpc %{on_off_switch system_ntirpc}

Name:		nfs-ganesha
Version:	2.4.1
Release:	11%{?dev:%{dev}}%{?dist}
Summary:	NFS-Ganesha is a NFS Server running in user space
Group:		Applications/System
License:	LGPLv3+
Url:		https://github.com/nfs-ganesha/nfs-ganesha/wiki
BuildArch:	x86_64

# BZ1372691
Obsoletes:	%{name}-docs

Source0:	https://github.com/%{name}/%{name}/archive/V%{version}/%{name}-%{version}.tar.gz
Source2:	rhgs-ganesha.conf
Source3:	rhgs-firewalld-nlm.xml
Source4:	rhgs-firewalld-rquota.xml
Source5:	rhgs-rhel-6-syconfig-ganesha.conf
Source6:	rhgs-rhel-7-syconfig-ganesha.conf
Patch001:	0001-FSAL_GLUSTER-Use-the-new-APIs-to-fetch-free-upcall-a.patch
Patch002:	0002-md-cache-Invalidate-ACL-incase-of-mode-change.patch
Patch003:	0003-add-export-Remove-an-extra-ref-in-case-of-failures.patch
Patch004:	0004-FSAL_GLUSTER-commit2-Fix-deferencing-a-NULL-pointer.patch
Patch005:	0005-MDCACHE-don-t-cache-large-directories.patch
Patch006:	0006-drc-fix-retire-window-accounting.patch
Patch007:       0007-Fix-use-after-free-during-shutdown.patch
Patch008:       0008-Always-lock-when-invalidating-dirents.patch
Patch009:       0009-State-Take-a-ref-on-an-object-for-each-state.patch
Patch010:       0010-md-cache-Do-lazy-deletion-of-the-new-entry-which-got.patch
Patch011:       0011-MDCACHE-Only-rename-dirents-if-both-found.patch
Patch012:       0012-FSAL_GLUSTER-Return-appropriate-error-in-case-if-loc.patch
Patch013:       0013-Don-t-do-work-in-assert.patch
Patch014:       0014-FSAL_GLUSTER-Avoid-double-free-of-handle-in-case-of-.patch
Patch015:       0015-MDCACHE-can-t-rename-dirent-if-not-in-cache.patch
Patch016:       0016-Fix-Coverity-CID-155159-Deadlock.patch
Patch017:       0017-Reduce-and-mitigate-a-rename-readir-race-window.patch
Patch018:       0018-MDCACHE-only-remove-cached-entry-on-unlink-on-succes.patch

BuildRequires:	cmake
BuildRequires:	bison
BuildRequires:	flex
BuildRequires:	pkgconfig
BuildRequires:	krb5-devel
BuildRequires:	dbus-devel
BuildRequires:	libcap-devel
BuildRequires:	libblkid-devel
BuildRequires:	libuuid-devel
%if %{with system_ntirpc}
BuildRequires:	libntirpc-devel >= 1.4.3
%endif
Requires:	dbus
Requires:	nfs-utils
%if ( 0%{?rhel} >= 7 )
Requires:	selinux-policy-targeted >= 3.13.1-60.el7_2.6
%endif
%if ( 0%{?fedora} ) || ( 0%{?rhel} && 0%{?rhel} >= 6 )
Requires:	rpcbind
%else
Requires:	portmap
%endif
%if %{with_nfsidmap}
BuildRequires:	libnfsidmap-devel
%else
BuildRequires:	nfs-utils-lib-devel
%endif
%if %{with rdma}
BuildRequires:	libmooshika-devel >= 0.6-0
%endif
%if %{with jemalloc}
BuildRequires:	jemalloc-devel
%endif
%if %{with_systemd}
BuildRequires: systemd
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd
%else
BuildRequires:	initscripts
%endif

%if %{with_firewalld}
%if 0%{?fedora}
Requires:    firewalld-filesystem
%else
Requires:    firewalld
%endif
%endif

# Use CMake variables

%description
nfs-ganesha : NFS-GANESHA is a NFS Server running in user space.
It comes with various back-end modules (called FSALs) provided as
shared objects to support different file systems and name-spaces.

%if %{with 9P}
%package mount-9P
Summary: a 9p mount helper
Group: Applications/System

%description mount-9P
This package contains the mount.9P script that clients can use
to simplify mounting to NFS-GANESHA. This is a 9p mount helper.
%endif

%if %{with vfs}
%package vfs
Summary: The NFS-GANESHA's VFS FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha = %{version}-%{release}

%description vfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support VFS based filesystems
%endif

%if %{with proxy}
%package proxy
Summary: The NFS-GANESHA's PROXY FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha = %{version}-%{release}

%description proxy
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PROXY based filesystems
%endif

%if %{with utils}
%package utils
Summary: The NFS-GANESHA's util scripts
Group: Applications/System
Requires:	dbus-python, pygobject2
%if %{with gui_utils}
BuildRequires:	PyQt4-devel
Requires:	PyQt4
%endif
BuildRequires:	python-devel
Requires: nfs-ganesha = %{version}-%{release}, python

%description utils
This package contains utility scripts for managing the NFS-GANESHA server
%endif

%if %{with lttng}
%package lttng
Summary: The NFS-GANESHA's library for use with LTTng
Group: Applications/System
BuildRequires: lttng-ust-devel >= 2.3
Requires: nfs-ganesha = %{version}-%{release}, lttng-tools >= 2.3,  lttng-ust >= 2.3

%description lttng
This package contains the libganesha_trace.so library. When preloaded
to the ganesha.nfsd server, it makes it possible to trace using LTTng.
%endif

# Option packages start here. use "rpmbuild --with gpfs" (or equivalent)
# for activating this part of the spec file

# NULL
%if %{with nullfs}
%package nullfs
Summary: The NFS-GANESHA's NULLFS Stackable FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description nullfs
This package contains a Stackable FSAL shared object to
be used with NFS-Ganesha. This is mostly a template for future (more sophisticated) stackable FSALs
%endif

# GPFS
%if %{with gpfs}
%package gpfs
Summary: The NFS-GANESHA's GPFS FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description gpfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support GPFS backend
%endif

# CEPH
%if %{with ceph}
%package ceph
Summary: The NFS-GANESHA's CephFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libcephfs1-devel >= 10.2.0

%description ceph
This package contains a FSAL shared object to
be used with NFS-Ganesha to support CephFS
%endif

# RGW
%if %{with rgw}
%package rgw
Summary: The NFS-GANESHA's Ceph RGW FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	librgw2-devel >= 10.2.0

%description rgw
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Ceph RGW
%endif

# XFS
%if %{with xfs}
%package xfs
Summary: The NFS-GANESHA's XFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libattr-devel xfsprogs-devel

%description xfs
This package contains a shared object to be used with FSAL_VFS
to support XFS correctly
%endif

# PANFS
%if %{with panfs}
%package panfs
Summary: The NFS-GANESHA's PANFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}

%description panfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PANFS
%endif

# GLUSTER
%if %{with gluster}
%package gluster
Summary: The NFS-GANESHA's GLUSTER FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	glusterfs-api-devel >= 3.8.4
BuildRequires:	libattr-devel, libacl-devel

%description gluster
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Gluster
%endif

%prep
%setup -q -n %{name}-%{version}
rm -rf contrib/libzfswrapper
%patch001 -p1 -b.1386635
%patch002 -p1 -b.1386699
%patch003 -p1 -b.1396968
%patch004 -p1 -b.1399138
%patch005 -p1 -b.1382912 -b b.1379673
%patch006 -p1 -b.1398846
%patch007 -p1 -b.1401160
%patch008 -p1 -b.1401160
%patch009 -p1 -b.1401160
%patch010 -p1 -b.1401160
%patch011 -p1 -b.1410741
%patch012 -p1 -b.1410389
%patch013 -p1 -b.1413350 -b b.1413502
%patch014 -p1 -b.1413846
%patch015 -p1 -b.1410741
%patch016 -p1 -b.1428808
%patch017 -p1 -b.1428798
%patch018 -p1 -b.1429377

%build
cd src && %cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo	\
	-DBUILD_CONFIG=rpmbuild				\
	-DUSE_FSAL_NULL=%{use_fsal_null}		\
	-DUSE_FSAL_ZFS=NO				\
	-DUSE_FSAL_XFS=%{use_fsal_xfs}			\
	-DUSE_FSAL_CEPH=%{use_fsal_ceph}		\
	-DUSE_FSAL_RGW=%{use_fsal_rgw}			\
	-DUSE_FSAL_GPFS=%{use_fsal_gpfs}		\
	-DUSE_FSAL_PANFS=%{use_fsal_panfs}		\
	-DUSE_FSAL_GLUSTER=%{use_fsal_gluster}		\
	-DUSE_SYSTEM_NTIRPC=%{use_system_ntirpc}	\
	-DUSE_9P_RDMA=%{use_rdma}			\
	-DUSE_LTTNG=%{use_lttng}			\
	-DUSE_ADMIN_TOOLS=%{use_utils}			\
	-DUSE_GUI_ADMIN_TOOLS=%{use_gui_utils}		\
	-DUSE_FSAL_VFS=%{use_fsal_vfs}			\
	-DUSE_FSAL_PROXY=%{use_fsal_proxy}		\
	-DUSE_DBUS=ON					\
	-DUSE_9P=%{use_fsal_9P}				\
	-DDISTNAME_HAS_GIT_DATA=OFF			\
%if %{with jemalloc}
	-DALLOCATOR=jemalloc
%endif

make %{?_smp_mflags} || make %{?_smp_mflags} || make

%install
mkdir -p %{buildroot}%{_sysconfdir}/ganesha/
mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
mkdir -p %{buildroot}%{_sysconfdir}/sysconfig
mkdir -p %{buildroot}%{_sysconfdir}/logrotate.d
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sbindir}
mkdir -p %{buildroot}%{_libdir}/ganesha
mkdir -p %{buildroot}%{_localstatedir}/run/ganesha
mkdir -p %{buildroot}%{_libexecdir}/ganesha

install -m 644 src/config_samples/logrotate_ganesha %{buildroot}%{_sysconfdir}/logrotate.d/ganesha
install -m 644 src/scripts/ganeshactl/org.ganesha.nfsd.conf %{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 755 src/scripts/nfs-ganesha-config.sh %{buildroot}%{_libexecdir}/ganesha

install -m 644 %{SOURCE2} %{buildroot}%{_sysconfdir}/ganesha/ganesha.conf

%if %{with 9P}
install -m 755 src/tools/mount.9P %{buildroot}%{_sbindir}/mount.9P
%endif
%if %{with vfs}
install -m 644 src/config_samples/vfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif
%if %{with rgw}
install -m 644 src/config_samples/rgw.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_systemd}
mkdir -p %{buildroot}%{_unitdir}
install -m 644 src/scripts/systemd/nfs-ganesha.service %{buildroot}%{_unitdir}/nfs-ganesha.service
install -m 644 src/scripts/systemd/nfs-ganesha-lock.service %{buildroot}%{_unitdir}/nfs-ganesha-lock.service
install -m 644 src/scripts/systemd/nfs-ganesha-config.service %{buildroot}%{_unitdir}/nfs-ganesha-config.service
install -m 644 %{SOURCE6} %{buildroot}%{_sysconfdir}/sysconfig/ganesha
%else
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 src/scripts/init.d/nfs-ganesha.el6 %{buildroot}%{_sysconfdir}/init.d/nfs-ganesha
install -m 644 %{SOURCE5} %{buildroot}%{_sysconfdir}/sysconfig/ganesha
%endif

%if %{with_firewalld}
mkdir -p %{buildroot}%{_prefix}/lib/firewalld/services
install -m 644 %{SOURCE3} %{buildroot}%{_prefix}/lib/firewalld/services/nlm.xml
install -m 644 %{SOURCE4} %{buildroot}%{_prefix}/lib/firewalld/services/rquota.xml
%endif

%if %{with utils} && 0%{?rhel} && 0%{?rhel} <= 6
%{!?__python2: %global __python2 /usr/bin/python2}
%{!?python2_sitelib: %global python2_sitelib %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}
%{!?python2_sitearch: %global python2_sitearch %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(1))")}
%endif

%if %{with xfs}
install -m 644 src/config_samples/xfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with ceph}
install -m 644 src/config_samples/ceph.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with gluster}
install -m 644 src/config_samples/logrotate_fsal_gluster %{buildroot}%{_sysconfdir}/logrotate.d/ganesha-gfapi
%endif

%if %{with gpfs}
install -m 755 src/scripts/gpfs-epoch %{buildroot}%{_libexecdir}/ganesha
install -m 644 src/config_samples/gpfs.conf	%{buildroot}%{_sysconfdir}/ganesha
install -m 644 src/config_samples/gpfs.ganesha.nfsd.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 src/config_samples/gpfs.ganesha.main.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 src/config_samples/gpfs.ganesha.log.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 src/config_samples/gpfs.ganesha.exports.conf	%{buildroot}%{_sysconfdir}/ganesha
%if ! %{with_systemd}
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 src/scripts/init.d/nfs-ganesha.gpfs		%{buildroot}%{_sysconfdir}/init.d/nfs-ganesha-gpfs
%endif
%endif

make -C src DESTDIR=%{buildroot} install

rm -f %{buildroot}/etc/ganesha/ganesha.conf.example

install -m 644 src/ChangeLog	%{buildroot}%{_defaultdocdir}/ganesha

%post
%if ( 0%{?rhel} >= 7 )
semanage boolean -m --on rpcd_use_fusefs
%endif
%if %{with_systemd}
%systemd_post nfs-ganesha.service
%systemd_post nfs-ganesha-lock.service
%systemd_post nfs-ganesha-config.service
%endif
%if %{with_firewalld}
#reload service files if firewalld running
if $(systemctl is-active firewalld 1>/dev/null 2>&1); then
    #firewalld-filesystem is not available for rhel7, so command used for reload.
    firewall-cmd  --reload
fi
%endif
killall -SIGHUP dbus-daemon 2>&1 > /dev/null

%preun
%if %{with_systemd}
%systemd_preun nfs-ganesha-lock.service
%endif

%postun
%if %{with_systemd}
%systemd_postun_with_restart nfs-ganesha-lock.service
%endif
%if %{with_firewalld}
#reload service files if firewalld running
if $(systemctl is-active firewalld 1>/dev/null 2>&1); then
    firewall-cmd  --reload
fi
%endif

%files
%{!?_licensedir:%global license %%doc}
%license src/LICENSE.txt
%{_bindir}/ganesha.nfsd
%exclude %{_libdir}/ganesha
%if ! %{with system_ntirpc}
%{_libdir}/libntirpc.so.1.4.1
%{_libdir}/libntirpc.so.1.4
%{_libdir}/libntirpc.so
%{_libdir}/pkgconfig/libntirpc.pc
%{_includedir}/ntirpc/
%endif
%config %{_sysconfdir}/dbus-1/system.d/org.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/sysconfig/ganesha
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha
%dir %{_sysconfdir}/ganesha/
%config(noreplace) %{_sysconfdir}/ganesha/ganesha.conf
%dir %{_defaultdocdir}/ganesha/
%{_defaultdocdir}/ganesha/*
%doc %{_defaultdocdir}/ganesha/ChangeLog
%dir %{_localstatedir}/run/ganesha
%dir %{_libexecdir}/ganesha/
%{_libexecdir}/ganesha/nfs-ganesha-config.sh

%if %{with_firewalld}
%{_prefix}/lib/firewalld/services/nlm.xml
%{_prefix}/lib/firewalld/services/rquota.xml
%endif

%if %{with_systemd}
%{_unitdir}/nfs-ganesha.service
%{_unitdir}/nfs-ganesha-lock.service
%{_unitdir}/nfs-ganesha-config.service
%else
%{_sysconfdir}/init.d/nfs-ganesha
%endif

%if %{with 9P}
%files mount-9P
%{_sbindir}/mount.9P
%endif

%if %{with vfs}
%files vfs
%{_libdir}/ganesha/libfsalvfs*
%config(noreplace) %{_sysconfdir}/ganesha/vfs.conf
%endif

%if %{with proxy}
%files proxy
%{_libdir}/ganesha/libfsalproxy*
%endif

# Optional packages
%if %{with nullfs}
%files nullfs
%{_libdir}/ganesha/libfsalnull*
%endif

%if %{with gpfs}
%files gpfs
%{_libdir}/ganesha/libfsalgpfs*
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.main.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.log.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.exports.conf
%{_libexecdir}/ganesha/gpfs-epoch
%if ! %{with_systemd}
%{_sysconfdir}/init.d/nfs-ganesha-gpfs
%endif
%endif

%if %{with xfs}
%files xfs
%{_libdir}/ganesha/libfsalxfs*
%config(noreplace) %{_sysconfdir}/ganesha/xfs.conf
%endif

%if %{with ceph}
%files ceph
%{_libdir}/ganesha/libfsalceph*
%config(noreplace) %{_sysconfdir}/ganesha/ceph.conf
%endif

%if %{with rgw}
%files rgw
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalrgw*
%config(noreplace) %{_sysconfdir}/ganesha/rgw.conf
%endif

%if %{with gluster}
%files gluster
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha-gfapi
%{_libdir}/ganesha/libfsalgluster*
%endif

%if %{with panfs}
%files panfs
%{_libdir}/ganesha/libfsalpanfs*
%endif

%if %{with lttng}
%files lttng
%{_libdir}/ganesha/libganesha_trace*
%endif

%if %{with utils}
%files utils
%{python2_sitelib}/Ganesha/*
%{python2_sitelib}/ganeshactl-*-info
%if %{with gui_utils}
%{_bindir}/ganesha-admin
%{_bindir}/manage_clients
%{_bindir}/manage_exports
%{_bindir}/manage_logger
%{_bindir}/ganeshactl
%{_bindir}/client_stats_9pOps
%{_bindir}/export_stats_9pOps
%endif
%{_bindir}/fake_recall
%{_bindir}/get_clientids
%{_bindir}/grace_period
%{_bindir}/purge_gids
%{_bindir}/ganesha_stats
%{_bindir}/sm_notify.ganesha
%{_bindir}/ganesha_mgr
%endif

%changelog
* Mon Jun 12 2017 Soumya Koduri <skoduri at redhat.com> 2.4.1-11
- Resolves: rhbz#1456342 (#errata build complains about >=1.4.3-4)

* Mon May 29 2017 Soumya Koduri <skoduri at redhat.com> 2.4.1-10
- Resolves: rhbz#1456342

* Wed Mar 8 2017 Soumya Koduri <skoduri at redhat.com> 2.4.1-9
- Resolves: rhbz#1429377

* Mon Mar 6 2017 Soumya Koduri <skoduri at redhat.com> 2.4.1-8
- Resolves: rhbz#1428808
- Resolves: rhbz#1428798

* Thu Feb 2 2017 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.4.1-7
- Resolves: rhbz#1410741

* Wed Jan 18 2017 Soumya Koduri <skoduri at redhat.com> 2.4.1-6
- Resolves: rhbz#1410389
- Resolves: rhbz#1413350
- Resolves: rhbz#1413846

* Fri Jan 13 2017 Soumya Koduri <skoduri at redhat.com> 2.4.1-5
- Resolves: rhbz#1410741

* Tue Jan 10 2017 Jiffin Tony Thottan <jthottan at redhat.com> 2.4.1-4
- Resolves memory corruption issues (Resolves: rhbz#1401160)

* Fri Dec 16 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.4.1-3
- NFS-Ganesha-2.4.1-3
- Resolves: rhbz#1382912
- Resolves: rhbz#1398846
- Resolves: rhbz#1379673
- Resolves: rhbz#1413502

* Fri Dec 2 2016 Jiffin Tony Thottan <jthottan at redhat.com> 2.4.1-2
- change anon_uid/gid to nfsnobody (Resolves: rhbz#1240258)
- use new api's to fetch upcall (Resolves: rhbz#1386635)
- md cache invalidate acl incase of mode change (Resolves: rhbz#1386699)
- add export remove an extra ref in case of failures (Resolves: rhbz#1396968)
- commit2 fix deferencing a NULL pointer (Resolves: rhbz#1399138)

* Wed Nov 2 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.4.1-1
- NFS-Ganesha-2.4.1
- Resolves: rhbz#1389468

* Fri Sep 23 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.4.0-2
- NFS-Ganesha-2.4.0, plus patch
- Resolves: rhbz#1357844
- Resolves: rhbz#1372691

* Thu Sep 22 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.4.0-1
- NFS-Ganesha-2.4.0
- Resolves: rhbz#1357844

* Tue Jun 7 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-8
- selinux-policy-targeted (#1336760)
- symlink op fails when ACL enabled (#1343314)

* Mon May 16 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-7
- selinux rpcd, selinux-policy->selinux-policy-targeted (#1336407)

* Tue May 10 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-6
- selinux rpcd (#1323740)

* Thu May 5 2016 Jiffin Tony Thottan <jthottan@redhat.com>
- Use option grace_period to determine grace timeout (#1329887)
- Configure glusterfs epoch script in sysconfig files (#1242358)
- Setting default for Entries_HWMark in ganesha conf file (#1240172)
- RPCSEC_GSS: Using kerberos validate principals but not handles (#1329950)

* Thu May 5 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-5
- selinux rpcd (#1323740)

* Thu Apr 14 2016 Niels de Vos <ndevos@redhat.com> 2.3.1-4
- prevent firewalld dependency on RHEL-6 (#1327074)

* Wed Apr 13 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-3
- Configure firewalld to open up ports required by nfs-ganesha (#1245103)
- logrotate ganesha-gfapi.lg (#1261858)

* Wed Mar 30 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-2
- missing /var/log/ganesha.log (nfs-ganesha-config.service) (#1321878)

* Wed Mar 16 2016 Kaleb S. KEITHLEY <kkeithle at redhat.com> 2.3.1-1
- Refresh of NFS-Ganesha sources to upstream version 2.3.1.
- nfs-ganesha.spec is updated based on upstream NFS-Ganesha community
- version

* Wed Dec 16 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-12
- MainNFSD: Unref export entry if pNFS DS is enabled (#1286244)
- [FSAL_GLUSTER] Handle ENOENT properly in getattrs (#1289483)

* Wed Nov 18 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-11
- [FSAL_GLUSTER] Add missed check during backport (#1276348)
- [FSAL_GLUSTER] Populate Allow ACL entries accordingly (#1277886)
- MainNFSD: Rightly use grace_period option for grace (#1276683)
- [FSAL_GLUSTER] Fix uninitialized variable (#1277886)
- use cmake macro to set correct compile flags (#1244792)
  by Niels de Vos <ndevos@redhat.com>

* Tue Nov 3 2015 Niels de Vos <ndevos@redhat.com>
- Build with correct -debuginfo subpackage (#1244792)

* Mon Oct 19 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-10
- MainNFSD: Cleanup RPCs before do_shutdown() (#1233533)
- [FSAL_GLUSTER]: Fix error handling in ACL APIs (#1242148)
- [FSAL_GLUSTER]: Remove earlier ACL implementation (#1251477)
- [FSAL_GLUSTER]: Cleanup ACL code (#1251471)
- [FSAL_GLUSTER]: Display Deny ACE entries (#1238561)

* Fri Sep 18 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-9
- [FSAL_GLUSER] Cleanup the glfd after glfs_close() (#1263094)

* Sat Sep 12 2015 Niels de Vos <ndevos@redhat.com> 2.2.0-8
- NLM: retry in nlm_send_async() after getaddrinfo() failure (#1257957)

* Mon Aug 31 2015 Niels de Vos <ndevos@redhat.com> 2.2.0-7
- Prevent building the *-nullfs subpackage and obsolete it

* Thu Aug 27 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-6
- [NFS-Ganesha] Do not let systemd restart ganesha on abort
- [NFS-Ganesha] Make it possible to disable FSAL_NULL during build
- [FSAL_GLUSTER] Enable subdir mount for symlinks

* Sat Jul 11 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-5
- [Common_SAL] Fix an issue with adjusting lock ranges
- [FSAL_GLUSTER] Include O_EXCL flag while creating a file
- [FSAL_GLUSTER] Bail out incase of larger lock ranges

* Mon Jul 6 2015 Meghana <mmadhusu@redhat.com>
- Adding a patch file misses in the previous commit.

* Sat Jul 4 2015 Meghana M <mmadhusu@redhat.com> 2.2.0-4
- [FSAL_GLUSTER] Fix isdir variable usage
- [FSAL_GLUSTER] Handling deadlinks
- [FSAL_GLUSTER] Fix inherit ACLs

* Mon Jun 15 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-3
- [FSAL_GLUSTER] Fix for dynamic-unexport crash
- [FSAL_GLUSTER] Improvements in ACL feature
- [FSAL_GLUSTER] Fix for excessive CPU Usage

* Thu Jun 4 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-2
- [FSAL_GLUSTER] Upcall interface to receive notifications
- [FSAL_GLUSTER] Fix position of gfapi.log
- [FSAL_GLUSTER] Support for NFSv4 ACLs
- [FSAL_GLUSTER] Fix for NFSv3 iozone test crash
- [FSAL_GLUSTER] pNFS enhancements

* Wed Jun 3 2015 Soumya Koduri <skoduri@redhat.com> 2.2.0-1
- Refresh of NFS-Ganesha sources to upstream version 2.2. nfs-ganesha.spec is updated
- based on upstream NFS-Ganesha community version by Niels de vos <ndevos@redhat.com>
- In addition ganesha.conf required in downstream builds has been included by
- Meghana Madhusudhan <mmadhusu@redhat.com>

* Mon Jun 9 2014 Meghana M <mmadhusu@redhat.com> 2.1.0.2-4
- Added org.ganesha.nfsd.conf as one of the sources.
- Added a README that explains how to enable Dbus for
- nfs-ganesha.

* Thu May 29 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.2-3
- Removed a sed expression which isnt required.

* Thu May 29 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.2-2
- Fix for rpm check_buildroot issue and added missing links of .so files

* Wed May 28 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.2-1
- Refresh of sources to NFS-Ganesha 2.1 RC1 and a coverity fix
- Changed the format of nfs-ganesha.conf from dos to unix.

* Fri May 23 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.1-2
- Fixed few issues in the NFS-Ganesha 2.1 spec file

* Mon May 12 2014 Meghana <mmadhusu@redhat.com> 2.1.0.1-1
- Refresh of sources to NFS-Ganesha 2.1 upstream release

* Mon May 5 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.1-1
- Refresh of sources to NFS-Ganesha 2.1 upstream release

* Mon Jan 27 2014 Anand Subramanian <anands@redhat.com> 2.0.0.2-4
- Fix for check_debug by Meghana and Soumya for validating debug levels

* Fri Jan 24 2014 Anand Subramanian <anands@redhat.com> 2.0.0.2-3
- Fix for debug level option num being greater than 10

* Thu Jan 23 2014 Anand Subramanian <anands@redhat.com> 2.0.0.2-2
- Fix for ports cleanup after ganesha is terminated using kill -s TERM

* Mon Jan 20 2014 Anand Subramanian <anands@redhat.com> 2.0.0.2-1
- Fix for showmount command fixed in code
- Fix for rpc ports cleanup in rhs-nfs-ganesha script and usage

* Mon Jan 6 2014 Anand Subramanian <anands@redhat.com> 2.0.0.1-5
- update to rhs-nfs-ganesha script, fixes from Meghana

* Mon Dec 23 2013 Anand Subramanian <anands@redhat.com> 2.0.0.1-4
- corrected file permissions for sample nfs-ganesha.conf file

* Fri Dec 20 2013 Anand Subramanian <anands@redhat.com> 2.0.0.1-3
- updated Release to 3 with changes to rhs-nfs_ganesha script

* Fri Dec 13 2013 Anand Subramanian <anands@redhat.com> - 2.0.0.1-2
- updated rhs ganesha start/stop script

* Thu Dec 12 2013 Bala.FA <barumuga at redhat.com> - 2.0.0-1
- Initial commit

