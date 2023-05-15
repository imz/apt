
libexecaptdir=$(libexecdir)/apt

EXTRA_DIST = indexcopy.cc indexcopy.h

libexecapt_PROGRAMS = apt-get
bin_PROGRAMS = apt-cache apt-cdrom apt-config apt-mark

if COMPILE_APTSHELL
bin_PROGRAMS += apt-pipe
libexecapt_PROGRAMS += apt-shell
endif
if COMPILE_STATIC
bin_PROGRAMS += apt-get-static apt-cache-static apt-cdrom-static
endif

# We care about warnings during LTO. They are like g++ warnings. The flags have
# to be prefixed with "-Wc," for libtool to pass them to g++ invoked for linking.
# just some basic flags:
WARN_LD_FLAGS = -Wc,-Werror -Wc,-Wall -Wc,-Wextra

AM_CFLAGS = $(WARN_CFLAGS)
AM_CXXFLAGS = $(WARN_CXXFLAGS)
AM_LDFLAGS = $(WARN_LD_FLAGS)
LDADD = ../apt-pkg/libapt-pkg.la $(RPMLIBS)

apt_get_SOURCES = apt-get.cc acqprogress.cc acqprogress.h
apt_cache_SOURCES = apt-cache.cc
apt_shell_SOURCES = apt-shell.cc acqprogress.cc acqprogress.h
apt_shell_LDADD = $(LDADD) -lreadline
apt_pipe_SOURCES = $(apt_shell_SOURCES) apt-pipe.c
apt_pipe_CPPFLAGS = -DAPT_PIPE
apt_pipe_LDADD = $(LDADD) -lsetproctitle
apt_config_SOURCES = apt-config.cc
apt_cdrom_SOURCES = apt-cdrom.cc rpmindexcopy.cc rpmindexcopy.h
apt_mark_SOURCES = apt-mark.cc

apt_get_static_SOURCES = $(apt_get_SOURCES)
apt_get_static_LDFLAGS = $(AM_LDFLAGS) -all-static
apt_cache_static_SOURCES = $(apt_cache_SOURCES)
apt_cache_static_LDFLAGS = $(AM_LDFLAGS) -all-static
apt_cdrom_static_SOURCES = $(apt_cdrom_SOURCES)
apt_cdrom_static_LDFLAGS = $(AM_LDFLAGS) -all-static
