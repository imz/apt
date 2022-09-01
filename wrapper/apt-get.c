/*
 * APT-RPM wrapper for apt-get to wait for all its processes to finish
 *
 * Copyright (c) 2022 Vitaly Chikunov <vt@altlinux.org>
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Kudos to Julio Merino for inspiration on his work on fixing Bazel process-
 * wrapper helper tool: https://jmmv.dev/2019/11/wait-for-process-group.html
 */

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static void set_signals(const sighandler_t handler)
{
	signal(SIGHUP,  handler);
	signal(SIGINT,  handler);
	signal(SIGPIPE, handler);
	signal(SIGQUIT, handler);
	signal(SIGTTIN, handler);
	signal(SIGTTOU, handler);
}

/* Return 1 if we are under systemd. */
static int sd_booted()
{
	return !faccessat(AT_FDCWD, "/run/systemd/system/", F_OK, AT_SYMLINK_NOFOLLOW);
}

int main(int argc, char **argv)
{
	if (!argc)
		errx(1, "argv is empty");

	/* Introduced in Linux v3.4 in 2012 by Lennart Poettering. */
	if (prctl(PR_SET_CHILD_SUBREAPER, 1) == -1)
		warn("prctl");

	/*
	 * Do not create pgrp nor tpgrp, taking what bash is created for us.
	 * In that case signals will go naturally to our children too, and we
	 * need just to ignore several signals to continue job of waiting for
	 * their completion.
	 */
	set_signals(SIG_IGN);

#ifndef WRAP
	/* Generic wrapper. */
	argc--;
	argv++;
	if (!argc)
		errx(1, "need arguments");
#else
	/* Wrap a particular binary. */
	argv[0] = WRAP;
#endif
	/*
	 * If there is no systemd the reaper will never return if any service is
	 * restarted, thus abandon reaping and fall-back to a simple exec.
	 */
	const pid_t pid = sd_booted() ? fork() : 0;
	if (pid == -1)
		err(1, "fork");
	if (!pid) {
		set_signals(SIG_DFL);
		execvp(argv[0], argv);
		err(127, "exec: %s", argv[0]); /* command not found */
	}

	/*
	 * Reap all remaining processes.
	 */
	int ret = 1;
	int wstatus = 0;
	pid_t wpid;
	while ((wpid = waitpid(-1, &wstatus, 0)) != -1 || errno == EINTR) {
		if (wpid == pid) {
			if (WIFEXITED(wstatus))
				ret = WEXITSTATUS(wstatus);
			else if (WIFSIGNALED(wstatus))
				ret = 128 + WTERMSIG(wstatus);
		}
	}
	if (errno != ECHILD)
		warn("wait");

	return ret;
}
