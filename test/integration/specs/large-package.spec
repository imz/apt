Name:      large-package
Version:   1
Release:   alt1
Summary:   Test package
License:   LGPLv2+
Group:     Other

BuildArch: noarch

AutoReq: no
AutoProv: no
# to make faster -- try to turn off compression (I've tried:
# * w0.gzdio, w.ufdio, w.fdio -- ok, but still slow
# * w0.lzdio, w0T8.xzdio -- extremely slow! (why?..)
# So, w.fdio must be the simplest and fastest I/O type.)
%global _binary_payload w.fdio

%description
Dummy description

%install
set -efuC -o pipefail

mkdir -p %buildroot%_datadir/%name

readonly BYTES_NEEDED=$(( 2 * 1024 * 1024 * 1024 ))
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
