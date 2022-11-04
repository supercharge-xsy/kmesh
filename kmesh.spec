%global debug_package %{nil}

Name:          kmesh
Version:       1.0.0
Release:       1
Summary:       %{name} is a eBPF-based service mesh kernel solution
License:       Mulan PSL v2
URL:           https://gitee.com/openeuler
Source0:       %{name}-%{version}.tar.gz

BuildRequires: make
BuildRequires: protobuf protobuf-c protobuf-c-devel
BuildRequires: golang >= 1.16
BuildRequires: clang >= 10.0.1 llvm >= 10.0.1
BuildRequires: libbpf-devel kernel-devel >= 5.10
BuildRequires: libboundscheck

Requires: bpftool
Requires: libbpf kernel >= 5.10
Requires: libboundscheck

%description
%{name} is a eBPF-based service mesh kernel solution.

ExclusiveArch: x86_64 aarch64

%prep
%autosetup -n %{name}-%{version}

%build
cd %{_builddir}/%{name}-%{version}
./build.sh -b

%install
mkdir -p %{buildroot}%{_bindir}
install %{_builddir}/%{name}-%{version}/kmesh-daemon %{buildroot}%{_bindir}
install %{_builddir}/%{name}-%{version}/kmesh-cmd %{buildroot}%{_bindir}
install %{_builddir}/%{name}-%{version}/build/kmesh-start-pre.sh %{buildroot}%{_bindir}
install %{_builddir}/%{name}-%{version}/build/kmesh-stop-post.sh %{buildroot}%{_bindir}

mkdir -p %{buildroot}/usr/lib64
install %{_builddir}/%{name}-%{version}/bpf/deserialization_to_bpf_map/libkmesh_deserial.so %{buildroot}/usr/lib64
install %{_builddir}/%{name}-%{version}/api/v2-c/libkmesh_api_v2_c.so %{buildroot}/usr/lib64

mkdir -p %{buildroot}/lib/modules/kmesh
install %{_builddir}/%{name}-%{version}/kernel/ko/kmesh.ko %{buildroot}/lib/modules/kmesh

mkdir -p %{buildroot}/%{_sysconfdir}/kmesh
install %{_builddir}/%{name}-%{version}/config/kmesh.json %{buildroot}/%{_sysconfdir}/kmesh

mkdir -p %{buildroot}/usr/lib/systemd/system
install %{_builddir}/%{name}-%{version}/build/kmesh.service %{buildroot}/usr/lib/systemd/system

%check
cd %{_builddir}/%{name}-%{version}
#make
#make test

%post
echo "installing ..."
ln -sf /lib/modules/kmesh/kmesh.ko /lib/modules/`uname -r`
depmod -a

%preun
if [ "$1" == "1" ]; then
    systemctl status kmesh | grep "active (running)"
    if [ "$?" == "0" ]; then
        systemctl restart kmesh.service
    fi
else
    systemctl stop kmesh.service
fi

%postun
if [ "$1" -ne "1" ]; then
    rm -rf /lib/modules/`uname -r`/kmesh.ko
fi
depmod -a

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%attr(0550,root,root) %{_bindir}/kmesh-daemon
%attr(0550,root,root) %{_bindir}/kmesh-cmd

%attr(0500,root,root) /usr/lib64/libkmesh_deserial.so
%attr(0500,root,root) /usr/lib64/libkmesh_api_v2_c.so

%attr(0550,root,root) %dir /lib/modules/kmesh
%attr(0440,root,root) /lib/modules/kmesh/kmesh.ko

%attr(0750,root,root) %dir %{_sysconfdir}/kmesh
%config(noreplace) %attr(0640,root,root) %{_sysconfdir}/kmesh/kmesh.json
%config(noreplace) %attr(0640,root,root) /usr/lib/systemd/system/kmesh.service
%attr(0550,root,root) /usr/bin/kmesh-start-pre.sh
%attr(0550,root,root) /usr/bin/kmesh-stop-post.sh

%changelog
* Mon Sep 13 2021 huangliming<huangliming5@huawei.com> - 1.0.0-1
- first package
