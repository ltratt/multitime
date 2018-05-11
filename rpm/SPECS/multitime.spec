Name:           multitime
Version:        1.3
Release:        1%{?dist}
Summary:        Time command execution over multiple executions

Group:          Utility
License:        MIT
URL:            http://tratt.net/laurie/src/multitime/
Source0:        https://github.com/ltratt/%{name}/archive/%{name}-%{version}/%{name}-%{version}.tar.gz
# Source0:        %%{name}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  automake gcc

%description
multitime is, in essence, a simple extension to time which runs a command
multiple times and prints the timing means (with confidence intervals),
standard deviations, minimums, medians, and maximums having done so. This can
give a much better understanding of the command's performance

%prep
             # This is what happens when you put the project name in the TAG! :(
%setup -q -n %{name}-%{name}-%{version}
make -f Makefile.bootstrap

%build
./configure --prefix=/usr
make -j $(nproc)

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT
make DESTDIR="${RPM_BUILD_ROOT}" install

# Hack until Makefile.in is patched
mkdir -p "${RPM_BUILD_ROOT}/usr/share/"
mv "${RPM_BUILD_ROOT}/usr/man" "${RPM_BUILD_ROOT}/usr/share/man"

%files
%{_bindir}/multitime
%{_mandir}/man1/multitime.1.gz

%changelog
* Fri May 11 2018 Andy Neff <andy@visionsystemsinc.com> - 1.3-1
- Initial RPM Release
