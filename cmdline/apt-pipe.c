/* ----------------------------------------------------------------------------
   $Id: apt-pipe.c,v 1.3 2005/03/20 20:56:03 me Exp $
 */

#ifndef APT_PIPE_PATH
#define APT_PIPE_PATH "/var/lib/apt/pipe"
#endif

#include <config.h>

#include <argz.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <setproctitle.h>

/* ----------------------------------------------------------------------------
*/

extern int aptpipe_init(void);
extern int aptpipe_main(int, const char **);
extern int aptpipe_fini(void);

/* ----------------------------------------------------------------------------
 */

static volatile sig_atomic_t signalled = 0;

/* ----------------------------------------------------------------------------
  server
*/
static void sighandler(int sig)
{
	++signalled;
}

static void set_sighandler(int flags)
{
	struct sigaction sa;

	sa.sa_handler = sighandler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);
	sa.sa_flags = flags;

	(void) sigaction(SIGINT, &sa, NULL);
	(void) sigaction(SIGTERM, &sa, NULL);
	(void) sigaction(SIGHUP, &sa, NULL);
}

static int do_listen()
{
	int servsock;
	struct sockaddr_un sockaddr;

	if ((servsock = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0)
		return -1;

	unlink(APT_PIPE_PATH);
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sun_family = AF_LOCAL;
	strncpy(sockaddr.sun_path, APT_PIPE_PATH, sizeof(sockaddr.sun_path));
	if (bind(servsock, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
		return -1;

	return ((listen(servsock, 1)) < 0 ? -1 : servsock);
}

static ssize_t recv_query(int sock, void *buf, size_t bufsize, int *fd)
{
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t	received = 0;

	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;
	struct cmsghdr	*cmsg;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	iov[0].iov_base = buf;
	iov[0].iov_len = bufsize;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	msg.msg_control = control_un.control;
	msg.msg_controllen = sizeof(control_un.control);

	if ((received = recvmsg(sock, &msg, MSG_WAITALL)) > 0 &&
		(cmsg = CMSG_FIRSTHDR(&msg)) != NULL &&
		cmsg->cmsg_len == CMSG_LEN(sizeof(int)) &&
		cmsg->cmsg_level == SOL_SOCKET &&
		cmsg->cmsg_type == SCM_RIGHTS)
		*fd = *((int *) CMSG_DATA(cmsg));

	return(received);
}

static int send_reply(int sock, char *buf, ssize_t bufsize, int fd)
{
	int i, ac;
	char **av = NULL;

	/* minimal sanity check */
	if (0 != *(buf + bufsize - 1))
		return -1;

	/* make fd passed by client our stdout/stderr */
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	close(fd);

	/* apt's .Parse skips av[0], so fake it */
	ac = argz_count(buf, bufsize) + 1;
	av = (char **)calloc(ac + 1, sizeof(char *));
	*av = "";

	argz_extract(buf, bufsize, ++av);

	ac = i = aptpipe_main(ac, (const char **)--av);
	fflush(stdout);
	fflush(stderr);

	free(av);

	if ((fd = open("/dev/null", O_RDWR)) < 0)
		return 1;

	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	close(fd);

	i = (i < 0);
	ac = (ac > 0);

	/* send last reply later */
	if (!ac)
		if (write(sock, &i, sizeof(i)) != sizeof(i))
			return -1;

	return(ac);
}

static int mainloop(int servsock) {
	int cl = -1;
	int done = 0;
	char buf[65536];

	while(!signalled && !done) {
		int fd = -1;
		size_t received;

		/* TODO check for pending errors on socket */

		/* enable EINTR while in accept */
		set_sighandler(0);
		if((cl = accept(servsock, NULL, 0)) < 0) continue;

		set_sighandler(SA_RESTART);
		if ((received = recv_query(cl, buf, sizeof(buf), &fd)) > 0 && fd != -1)
			done = send_reply(cl, buf, received, fd);
		if (!done) {
			close(cl);
			cl = -1;
		}
	}

	close(servsock);
	return(cl);
}

static int daemonize()
{
	pid_t pid;
	int i, fd;
	int fds[2] = {-1, -1};

	if (pipe(fds) < 0)
		return -1;

	if ((pid = fork()) < 0)
		return -1;

	if (pid) {
		/* parent */
		close(fds[1]);
		/* get child's status */
		if (read(fds[0], &i, (sizeof(i))) != sizeof(i))
			return -1;
		return i;
	}

	/* child */
	close(fds[0]);
	setsid();
	if (chdir("/"))
		exit(1);
	while (fds[1] <= 2) {
		fds[1] = dup(fds[1]);
		if (fds[1] < 0)
			exit(1);
	}

	if ((fd = open("/dev/null", O_RDWR)) < 0)
		exit(1);

	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	/* closeall */
	i = sysconf (_SC_OPEN_MAX);
	for (fd = 3; fd < i; fd++)
		if (fd != fds[1])
			close (fd);

	/* ignore some signals */
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	/* no EINTR please */
	set_sighandler(SA_RESTART);

	/* open listening socket */
	if ((fd = do_listen()) < 0)
		exit(1);

	/* init apt */
	if (aptpipe_init() < 0)
		exit(1);

	/* clean up proc title */
	setproctitle("%s", "ready");

	/* we're still alive, notify parent */
	i = 0;
	if (write(fds[1], &i, sizeof(i)) != sizeof(i))
		exit(1);
	close(fds[1]);

	/* enter main loop */
	fd = mainloop(fd);

	/* cleanup */
	aptpipe_fini();
	unlink(APT_PIPE_PATH);
	if (fd >= 0)
		if (write(fd, &i, sizeof(i)) != sizeof(i))
			exit(1);
	exit(EXIT_SUCCESS);
}

/* ----------------------------------------------------------------------------
   client
*/
static int do_connect()
{
	int sock;
	struct sockaddr_un servaddr;

	if ((sock = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0)
		return sock;

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strncpy(servaddr.sun_path, APT_PIPE_PATH, sizeof(servaddr.sun_path));
	for(;;) {
		if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
			/* ENOENT ECONNREFUSED : (re)spawn daemon */
			if (errno == ENOENT || errno == ECONNREFUSED) {
				if (daemonize() < 0) {
					fprintf(stderr, "daemonize(): %s\n", strerror(errno));
					exit(1);
				}
				continue;
			} else {
				/* EACCESS etc -- just die */
				fprintf(stderr, "connect(): %s\n", strerror(errno));
				exit(1);
			}
		}
		break;
	}

	return sock;
}

static ssize_t send_query(int fd, int ac, char *av[])
{
	int i;
	struct msghdr msg;
	struct iovec *iov = NULL;

	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;
	struct cmsghdr	*cmsg;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	if ((iov = (struct iovec *)calloc(ac, sizeof(struct iovec))) == NULL)
		return -1;

	msg.msg_iov = iov;
	msg.msg_iovlen = ac;

	for (i=0; i < ac; iov++, i++) {
		iov->iov_base = (void *)av[i];
		iov->iov_len = strlen(av[i]) + 1;
	}

	/* keep final 0 for a while */
	(--iov)->iov_len--;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	if (sendmsg(fd, &msg, 0) < 0)
		return -1;

	/* pass fd and final 0 */
	i = 0;
	iov = msg.msg_iov;
	iov->iov_base = &i;
	iov->iov_len = 1;
	msg.msg_iovlen = 1;

	msg.msg_control = control_un.control;
	msg.msg_controllen = sizeof(control_un.control);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*((int *) CMSG_DATA(cmsg)) = STDOUT_FILENO;

	return (sendmsg(fd, &msg, 0));
}

static int recv_reply(int fd)
{
	int i;

	if (read(fd, &i, (sizeof(i))) != sizeof(i))
		return -1;
	return i;
}

/*----------------------------------------------------------------------------*/
int main(int ac, char *av[])
{
	int i, fd;

	if (ac < 2) {
		fprintf(stderr, "usage: %s <query>\n", av[0]);
		exit(EXIT_FAILURE);
	}

	if ((fd = do_connect()) < 0) {
		fprintf(stderr, "do_connect: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* pass our query in av[] and stdout fd to server */
	if (send_query(fd, --ac, ++av) < 0) {
		fprintf(stderr, "send_query: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* wait for status responce
	   actual server reply will be passed via passed stdout */
	if ((i = recv_reply(fd)) < 0) {
		fprintf(stderr, "recv_reply\n");
		exit(EXIT_FAILURE);
	}

	return i;
}
