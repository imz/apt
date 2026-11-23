
methodsdir=${libdir}/apt/methods
methods_PROGRAMS = cdrom copy file ftp gpg gzip http https rsh rsync

# It's better to cd below because LN_S can fallback on ln or cp.
# The target dir already includes the installation prefix (due to libdir).
install-data-hook: | install-methodsPROGRAMS
	(cd $(methodsdir) \
	&& $(LN_S) rsh ssh \
	&& $(LN_S) gzip bzip2 \
	&& $(LN_S) gzip xz \
	&& $(LN_S) gzip zstd \
	)

# We care about warnings during LTO. They are like g++ warnings. The flags have
# to be prefixed with "-Wc," for libtool to pass them to g++ invoked for linking.
# just some basic flags:
WARN_LD_FLAGS = -Wc,-Werror -Wc,-Wall -Wc,-Wextra

AM_CFLAGS = $(WARN_CFLAGS)
AM_CXXFLAGS = $(WARN_CXXFLAGS)
AM_LDFLAGS = $(WARN_LD_FLAGS)
LDADD = ../apt-pkg/libapt-pkg.la

connectlib_SOURCES = \
	       rfc2553emu.cc \
	       rfc2553emu.h \
	       connect-debug.cc \
	       connect-debug.h \
	       connect.cc \
	       connect.h

cdrom_SOURCES = cdrom.cc
copy_SOURCES = copy.cc
file_SOURCES = file.cc
gpg_SOURCES = gpg.cc
gzip_SOURCES = gzip.cc
rsh_SOURCES = rsh.cc rsh.h
rsync_SOURCES = rsync.cc rsync-method.h

http_SOURCES = \
	       http.cc \
	       http.h \
	       $(connectlib_SOURCES)
http_LDADD = $(LDADD) @SOCKETLIBS@

https_SOURCES = \
	       http.cc \
	       http.h \
	       $(connectlib_SOURCES)
https_LDADD = $(LDADD) @SOCKETLIBS@ @TLSLIBS@
https_CPPFLAGS = -DUSE_TLS

ftp_SOURCES = \
	       ftp.cc \
	       ftp.h \
	       $(connectlib_SOURCES)
ftp_LDADD = $(LDADD) @SOCKETLIBS@
