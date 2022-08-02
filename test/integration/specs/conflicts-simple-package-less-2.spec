Name:      conflicts-simple-package-less-2
Version:   1
Release:   alt1
Summary:   Test package
License:   LGPLv2+
Group:     Other

Conflicts: simple-package < 2
# just an arbitrary package in order to invoke MarkInstallRec()
Requires: simple-package-noarch

%description
Dummy description

%files

%changelog
* Mon Sep 30 2019 Nobody <somebody@altlinux.org> 1-alt1
- Test package created
