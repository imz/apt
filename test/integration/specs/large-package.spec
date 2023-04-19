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
set -efuC -o pipefail

mkdir -p %buildroot%_datadir/%name

readonly BYTES_NEEDED=$(( 2 * 1024 * 1024 ))
{
    # Generate an uncompressible sequence of bytes,
    # so that the resulting package is that large.
    openssl enc -pbkdf2 -aes-256-ctr -nosalt \
	    -pass pass:myseed < /dev/zero 2>/dev/null \
	|| [ 1 -eq $? ] # ignore the error when the pipe is closed
} | head -c "$BYTES_NEEDED" >%buildroot%_datadir/%name/large

# make sure there are enough bytes for our test
[ "$BYTES_NEEDED" -le "$(wc -c <%buildroot%_datadir/%name/large)" ]

%files
%_datadir/%name

%changelog
* Mon Sep 30 2019 Nobody <nobody@altlinux.org> 1-alt1
- Test package created
