%define __arch_install_post /usr/lib/rpm/check-rpaths /usr/lib/rpm/check-buildroot

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

# Conditionally enable some FSALs, disable others.
#
# 1. rpmbuild accepts these options (gpfs as example):
#    --with gpfs
#    --without gpfs

%define on_off_switch() %%{?with_%1:ON}%%{!?with_%1:OFF}

# A few explanation about %bcond_with and %bcond_without
# /!\ be careful: this syntax can be quite messy
# %bcond_with means you add a "--with" option, default = without this feature
# %bcond_without adds a"--without" so the feature is enabled by default

%bcond_with nullfs
%global use_fsal_null %{on_off_switch nullfs}

%bcond_with gpfs
%global use_fsal_gpfs %{on_off_switch gpfs}

%bcond_with zfs
%global use_fsal_zfs %{on_off_switch zfs}

%bcond_with xfs
%global use_fsal_xfs %{on_off_switch xfs}

%bcond_with ceph
%global use_fsal_ceph %{on_off_switch ceph}

%bcond_with lustre
%global use_fsal_lustre %{on_off_switch lustre}

%bcond_with shook
%global use_fsal_shook %{on_off_switch shook}

%bcond_without gluster
%global use_fsal_gluster %{on_off_switch gluster}

%bcond_with hpss
%global use_fsal_hpss %{on_off_switch hpss}

%bcond_with panfs
%global use_fsal_panfs %{on_off_switch panfs}

%bcond_with pt
%global use_fsal_pt %{on_off_switch pt}

%bcond_with rdma
%global use_rdma %{on_off_switch rdma}

%bcond_with jemalloc

%bcond_with lustre_up
%global use_lustre_up %{on_off_switch lustre_up}

%bcond_with lttng
%global use_lttng %{on_off_switch lttng}

%bcond_with utils
%global use_utils %{on_off_switch utils}

%bcond_with gui_utils
%global use_gui_utils %{on_off_switch gui_utils}

%bcond_with 9p
%global use_9p %{on_off_switch 9p}

%global dev_version %{lua: extraver = string.gsub('', '%-', '.'); print(extraver) }

%define ntirpcname ntirpc
%define ntirpcvers 1.2.0

Name:		nfs-ganesha
Version:	2.2.0
Release:	12%{?dist}
Summary:	NFS-Ganesha is a NFS Server running in user space
Group:		Applications/System
License:	LGPLv3+
Url:		https://github.com/nfs-ganesha/nfs-ganesha/wiki

ExclusiveArch:  x86_64 aarch64
Source0:	https://github.com/nfs-ganesha/%{name}/archive/V%{version}/%{name}-V%{version}.tar.gz
Source1:	https://github.com/nfs-ganesha/%{ntirpcname}/archive/v%{ntirpcvers}/%{ntirpcname}-v%{ntirpcvers}.tar.gz
Source2:	rhgs-ganesha.conf
Patch001:       0001-Upcall-interface.patch
Patch002:       0002-gfapi-log.patch
Patch003:       0003-global-options.patch
Patch004:       0004-ACL-support-for-files-rebased.patch
Patch005:       0005-ACL-support-for-dir.patch
Patch006:       0006-fix-iozone-crash.patch
Patch007:       0007-pNFS-performace.patch
Patch008:       0008-fix-dynamic-unexport-crash.patch
Patch009:       0009-Improvements-in-acl-feature.patch
Patch010:       0010-Fix-Excessive-CPU-Usage.patch
Patch011:       0011-Fix-isdir-variable-usage-ACL.patch
Patch012:       0012-Handling-deadlinks-ACL.patch
Patch013:       0013-Fix-inherit-ACL.patch
Patch014:       0014-Fix-lock-ranges.patch
Patch015:       0015-Include-O_EXCL-during-create.patch
Patch016:       0016-Bail-out-incase-of-larger-lock-ranges.patch
Patch017:       0017-systemd-restart-fix.patch
Patch018:       0018-Disable-FSAL_NULL.patch
Patch019:       0019-Enable-symlink-mount.patch
Patch020:       0020-NLM-retry-in-nlm_send_async-after-getaddrinfo-failur.patch
Patch021:       0021-glfs-close-fix.patch
Patch022:       0022-Clean-RPC.patch
Patch023:       0023-ACL-error-handling.patch
Patch024:       0024-Remove-earlier-ACL-implementation.patch
Patch025:       0025-ACL-cleanup.patch
Patch026:       0026-Display-DENY-ACEs.patch
Patch027:       0027-fix-strict-compile-errors.patch
Patch028:       0028-ACL-correction.patch
Patch029:       0029-Populate-ALLOW-acl-entries.patch
Patch030:       0030-Fix-grace_period-option-usage.patch
Patch031:       0031-Fix-uninitialized-variable
Patch032:       0032-Fix-exports_init.patch
Patch033:       0033-Handle-ENOENT-in-GETATTR.patch

Provides:	bundled(libntirpc) = %{ntirpcvers}

Obsoletes:	nfs-ganesha-nullfs < %{version}-%{release}

BuildRequires:	cmake
BuildRequires:	bison flex
BuildRequires:	flex
BuildRequires:	pkgconfig
BuildRequires:	krb5-devel
BuildRequires:	dbus-devel
BuildRequires:	libcap-devel
BuildRequires:	libblkid-devel
BuildRequires:	libuuid-devel
Requires:	dbus
Requires:	nfs-utils
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
%if %{with lustre_up}
BuildRequires: lcap-devel >= 0.1-0
%endif
%if %{with_systemd}
BuildRequires: systemd
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd
%else
BuildRequires:	initscripts
%endif

# Use CMake variables

%description
nfs-ganesha : NFS-GANESHA is a NFS Server running in user space.
It comes with various back-end modules (called FSALs) provided as
 shared objects to support different file systems and name-spaces.

%if %{with 9p}
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
%if %{with gui_utils}
BuildRequires:	PyQt4-devel
Requires:	PyQt4
%endif
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

# Option packages start here. use "rpmbuild --with lustre" (or equivalent)
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

# ZFS
%if %{with zfs}
%package zfs
Summary: The NFS-GANESHA's ZFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libzfswrap-devel

%description zfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support ZFS
%endif

# CEPH
%if %{with ceph}
%package ceph
Summary: The NFS-GANESHA's CEPH FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
Requires:	ceph >= 0.78
BuildRequires:	ceph-devel >= 0.78

%description ceph
This package contains a FSAL shared object to
be used with NFS-Ganesha to support CEPH
%endif

# LUSTRE
%if %{with lustre}
%package lustre
Summary: The NFS-GANESHA's LUSTRE FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
Requires:	lustre
BuildRequires:	libattr-devel lustre

%description lustre
This package contains a FSAL shared object to
be used with NFS-Ganesha to support LUSTRE
%endif

# SHOOK
%if %{with shook}
%package shook
Summary: The NFS-GANESHA's LUSTRE/SHOOK FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
Requires:	lustre shook-client
BuildRequires:	libattr-devel lustre shook-devel

%description shook
This package contains a FSAL shared object to
be used with NFS-Ganesha to support LUSTRE via SHOOK
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

# HPSS
%if %{with hpss}
%package hpss
Summary: The NFS-GANESHA's HPSS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
#BuildRequires:	hpssfs

%description hpss
This package contains a FSAL shared object to
be used with NFS-Ganesha to support HPSS
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

# PT
%if %{with pt}
%package pt
Summary: The NFS-GANESHA's PT FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}


%description pt
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PT
%endif

# GLUSTER
%if %{with gluster}
%package gluster
Summary: The NFS-GANESHA's GLUSTER FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	glusterfs-api-devel >= 3.5.1
BuildRequires:	libattr-devel

%description gluster
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Gluster
%endif

%prep
%setup -q -a 1
mv %{ntirpcname}-%{ntirpcvers}/* src/libntirpc/
# 1228150 - Upcall infrastructure support
%patch001 -p1 -b.bz1228150
# 1228153 - Fix position of gfapi.log
%patch002 -p1 -b.bz1228153
# 1228155 - Support for NFSv4 ACLs
%patch003 -p1 -b.bz1228155
%patch004 -p1 -b.bz1228155
%patch005 -p1 -b.bz1228155
# 1214616 - Fix NFSv3 iozone crash
%patch006 -p1 -b.bz1214616
# 1228222 - Disable pNFS by default
%patch007 -p1 -b.bz1228222
# 1224183 - fix dynamic-unexport crash
%patch008 -p1 -b.bz1224183
# 1229569 - Improvements in ACL feature
%patch009 -p1 -b.bz1229569
# 1231651 - Fix Excessive CPU Usage
%patch010 -p1 -b.bz1231651
# 1234916 - Fix isdir variable usage
%patch011 -p1 -b.bz1234916
# 1238156 - Handling deadlinks when ACL is enabled
%patch012 -p1 -b.bz1238156
# 1238149 - Fix memory corruption for inherit ACL
%patch013 -p1 -b.bz1238149
# 1235121 - Fix an issue with adjusting lock ranges
%patch014 -p1 -b.bz1235121
# 1235121 - Include O_EXCL flag while creating a file
%patch015 -p1 -b.bz1235121
# 1235121 - Bail out incase of larger lock ranges
%patch016 -p1 -b.bz1235121
# 1242749 - Do not let systemd restart ganesha on abort
%patch017 -p1 -b.bz1242749
# 1247574 - Make it possible to disable FSAL_NULL during build
%patch018 -p1 -b.bz1247574
# 1241871 - Enable subdir mount for symlinks
%patch019 -p1 -b.bz1241871
# 1257957 - nfs-ganesha process gets killed while executing UNLOCK with a cthon test on vers=3
%patch020 -p1 -b.bz1257957
# 1263094 - Cleanup the glfd after glfs_close()
%patch021 -p1 -b.bz1263094
# 1233533 - Cleanup RPCs before do_shutdown()
%patch022 -p1 -b.bz1233533
# 1242148 - Fix error handling in ACL APIs
%patch023 -p1 -b.bz1242148
# 1251477 - Remove earlier ACL implementation 
%patch024 -p1 -b.bz1251477
# 1251471 - Cleanup ACL code
%patch025 -p1 -b.bz1251471
# 1238561 - Display Deny ACE entries
%patch026 -p1 -b.bz1238561
# 1244792 - build with correct compile flags, fix false rhel6 compiler errors
%patch027 -p1 -b.bz1244792
# 1276348 - Add missed check during backport
%patch028 -p1 -b.bz1276348
# 1277886 - Populate Allow ACL entries accordingly
%patch029 -p1 -b.bz1277886
# 1276683 - Rightly use grace_period option for grace
%patch030 -p1 -b.bz1276683
# 1277886 - Fix uninitialized variable
%patch031 -p1 -b.bz1277886
# 1286244 - Unref export entry if pNFS DS is enabled
%patch032 -p1 -b.bz1286244
# 1289483 - Handle ENOENT properly in getattrs
%patch033 -p1 -b.bz1289483

%build
# need to set CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT so that /etc and /var
# are not placed under /usr.
%cmake src						\
	-DCMAKE_BUILD_TYPE=Maintainer			\
	-DBUILD_CONFIG=rpmbuild				\
	-DCMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT=ON\
	-DUSE_FSAL_NULL=%{use_fsal_null}		\
	-DUSE_FSAL_ZFS=%{use_fsal_zfs}			\
	-DUSE_FSAL_XFS=%{use_fsal_xfs}			\
	-DUSE_FSAL_CEPH=%{use_fsal_ceph}		\
	-DUSE_FSAL_LUSTRE=%{use_fsal_lustre}		\
	-DUSE_FSAL_SHOOK=%{use_fsal_shook}		\
	-DUSE_FSAL_GPFS=%{use_fsal_gpfs}		\
	-DUSE_FSAL_HPSS=%{use_fsal_hpss}		\
	-DUSE_FSAL_PANFS=%{use_fsal_panfs}		\
	-DUSE_FSAL_PT=%{use_fsal_pt}			\
	-DUSE_FSAL_GLUSTER=%{use_fsal_gluster}		\
	-DUSE_9P_RDMA=%{use_rdma}			\
	-DUSE_FSAL_LUSTRE_UP=%{use_lustre_up}		\
	-DUSE_LTTNG=%{use_lttng}			\
	-DUSE_ADMIN_TOOLS=%{use_utils}			\
	-DUSE_GUI_ADMIN_TOOLS=%{use_gui_utils}		\
	-DUSE_FSAL_VFS=OFF				\
	-DUSE_FSAL_PROXY=OFF				\
	-DUSE_DBUS=ON					\
	-DUSE_9P=%{use_9p}				\
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
install -m 644 src/config_samples/logrotate_ganesha		%{buildroot}%{_sysconfdir}/logrotate.d/ganesha
install -m 644 src/scripts/ganeshactl/org.ganesha.nfsd.conf	%{buildroot}%{_sysconfdir}/dbus-1/system.d

install -m 644 %{SOURCE2} %{buildroot}%{_sysconfdir}/ganesha/ganesha.conf

%if %{with_systemd}
mkdir -p %{buildroot}%{_unitdir}
install -m 644 src/scripts/systemd/nfs-ganesha.service		%{buildroot}%{_unitdir}/nfs-ganesha.service
install -m 644 src/scripts/systemd/nfs-ganesha-lock.service	%{buildroot}%{_unitdir}/nfs-ganesha-lock.service
install -m 644 src/scripts/systemd/sysconfig/nfs-ganesha	%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%else
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 src/scripts/init.d/nfs-ganesha			%{buildroot}%{_sysconfdir}/init.d/nfs-ganesha
install -m 644 src/scripts/init.d/sysconfig/ganesha		%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%endif

%if %{with utils} && 0%{?rhel} && 0%{?rhel} <= 6
%{!?__python2: %global __python2 /usr/bin/python2}
%{!?python2_sitelib: %global python2_sitelib %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}
%{!?python2_sitearch: %global python2_sitearch %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(1))")}
%endif

%if %{with pt}
install -m 755 ganesha.pt.init %{buildroot}%{_sysconfdir}/init.d/nfs-ganesha-pt
install -m 644 config_samples/pt.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with xfs}
install -m 644 config_samples/xfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with zfs}
install -m 644 config_samples/zfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with ceph}
install -m 644 config_samples/ceph.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with lustre}
install -m 755 config_samples/lustre.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with gpfs}
install -m 644 config_samples/gpfs.conf	%{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.nfsd.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.main.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.log.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.exports.conf	%{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with utils}
pushd .
cd scripts/ganeshactl/
python setup.py --quiet install --root=%{buildroot}
popd
install -m 755 Protocols/NLM/sm_notify.ganesha		%{buildroot}%{_bindir}/sm_notify.ganesha
%endif

make DESTDIR=%{buildroot} install

rm -f %{buildroot}/etc/ganesha/ganesha.conf.example


%post
%if %{with_systemd}
%systemd_post nfs-ganesha.service
%systemd_post nfs-ganesha-lock.service
%endif

%preun
%if %{with_systemd}
%systemd_preun nfs-ganesha-lock.service
%endif

%postun
%if %{with_systemd}
%systemd_postun_with_restart nfs-ganesha-lock.service
%endif

%files
%defattr(-,root,root,-)
%{_bindir}/ganesha.nfsd
%exclude %{_bindir}/libntirpc.a
%config %{_sysconfdir}/dbus-1/system.d/org.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/sysconfig/ganesha
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha
%dir %{_sysconfdir}/ganesha/
%config(noreplace) %{_sysconfdir}/ganesha/ganesha.conf
%dir %{_defaultdocdir}/ganesha/
%{_defaultdocdir}/ganesha/*
%dir %{_localstatedir}/run/ganesha

%if %{with_systemd}
%{_unitdir}/nfs-ganesha.service
%{_unitdir}/nfs-ganesha-lock.service
%else
%{_sysconfdir}/init.d/nfs-ganesha
%endif

%if %{with 9p}
%files mount-9P
%defattr(-,root,root,-)
%{_sbindir}/mount.9P
%endif


%if %{with vfs}
%files vfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalvfs*
%config(noreplace) %{_sysconfdir}/ganesha/vfs.conf
%endif


%if %{with proxy}
%files proxy
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalproxy*
%endif


# Optional packages
%if %{with nullfs}
%files nullfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalnull*
%endif

%if %{with gpfs}
%files gpfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalgpfs*
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.main.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.log.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.exports.conf
%endif

%if %{with zfs}
%files zfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalzfs*
%config(noreplace) %{_sysconfdir}/ganesha/zfs.conf
%endif

%if %{with xfs}
%files xfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalxfs*
%config(noreplace) %{_sysconfdir}/ganesha/xfs.conf
%endif

%if %{with ceph}
%files ceph
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalceph*
%config(noreplace) %{_sysconfdir}/ganesha/ceph.conf
%endif

%if %{with lustre}
%files lustre
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/ganesha/lustre.conf
%{_libdir}/ganesha/libfsallustre*
%endif

%if %{with shook}
%files shook
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalshook*
%endif

%if %{with gluster}
%files gluster
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalgluster*
%endif

%if %{with hpss}
%files hpss
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalhpss*
%endif

%if %{with panfs}
%files panfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalpanfs*
%endif

%if %{with pt}
%files pt
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalpt*
%config(noreplace) %{_sysconfdir}/init.d/nfs-ganesha-pt
%config(noreplace) %{_sysconfdir}/ganesha/pt.conf
%endif

%if %{with lttng}
%files lttng
%defattr(-,root,root,-)
%{_libdir}/ganesha/libganesha_trace*
%endif

%if %{with utils}
%files utils
%defattr(-,root,root,-)
%{python2_sitelib}/Ganesha/*
%{python2_sitelib}/ganeshactl-*-info
%if %{with gui_utils}
%{_bindir}/ganesha-admin
%{_bindir}/manage_clients
%{_bindir}/manage_exports
%{_bindir}/manage_logger
%{_bindir}/ganeshactl
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
  Adding a patch file misses in the previous commit.

* Sat Jul 4 2015 Meghana M <mmadhusu@redhat.com> 2.2.0-4
  [FSAL_GLUSTER] Fix isdir variable usage
  [FSAL_GLUSTER] Handling deadlinks
  [FSAL_GLUSTER] Fix inherit ACLs

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

* Mon Jun 9 2014 Meghana M  <mmadhusu@redhat.com> 2.1.0.2-4
- Added org.ganesha.nfsd.conf as one of the sources.
  Added a README that explains how to enable Dbus for
  nfs-ganesha.

* Thu May 29 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.2-3
- Removed a sed expression which isnt required.

* Thu May 29 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.2-2
- Fix for rpm check_buildroot issue and added missing links of .so files

* Wed May 28 2014 Soumya Koduri <skoduri@redhat.com> 2.1.0.2-1
- Refresh of sources to NFS-Ganesha 2.1 RC1 and a coverity fix
  Changed the format of nfs-ganesha.conf from dos to unix.

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
