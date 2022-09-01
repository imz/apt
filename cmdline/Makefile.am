
EXTRA_DIST = indexcopy.cc indexcopy.h

bin_PROGRAMS = apt-get apt-cache apt-cdrom apt-config apt-mark

if COMPILE_APTSHELL
bin_PROGRAMS += apt-shell apt-pipe
endif
if COMPILE_STATIC
bin_PROGRAMS += apt-get-static apt-cache-static apt-cdrom-static
endif

AM_CFLAGS = $(WARN_CFLAGS)
AM_CXXFLAGS = $(WARN_CXXFLAGS)
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
apt_get_static_LDFLAGS = -all-static
apt_cache_static_SOURCES = $(apt_cache_SOURCES)
apt_cache_static_LDFLAGS = -all-static
apt_cdrom_static_SOURCES = $(apt_cdrom_SOURCES)
apt_cdrom_static_LDFLAGS = -all-static
