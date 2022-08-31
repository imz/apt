Name:      large-package
Version:   1
Release:   alt1
Summary:   Test package
License:   LGPLv2+
Group:     Other

BuildArch: noarch

AutoReq: no
AutoProv: no

%description
Dummy description

%install
mkdir -p %buildroot%_datadir/%name
echo a >%buildroot%_datadir/%name/large

%files
%_datadir/%name

%changelog
* Mon Sep 30 2019 Nobody <nobody@altlinux.org> 1-alt1
- Test package created
