/*
 * Simple Socket Server
 */

#include <usual/socket.h>
#include <usual/event.h>
#include <usual/time.h>
#include <usual/err.h>
#include <usual/safeio.h>
#include <usual/logging.h>
#include <usual/getopt.h>

struct WClient {
	struct event ev;
	int fd;
	uint8_t buf[2048];
	unsigned int bufpos;
};

static int make_socket(int domain, int type, int proto, bool nonblock)
{
#ifdef SOCK_CLOEXEC
	static int sock_cloexec = SOCK_CLOEXEC;
#else
	static int sock_cloexec = 0;
#endif
#ifdef SOCK_NONBLOCK
	static int sock_nonblock = SOCK_NONBLOCK;
#else
	static int sock_nonblock = 0;
#endif
	int sock, res, val, extra;

	extra = sock_cloexec | (nonblock ? sock_nonblock : 0);

loop:
	/* try to set cloexec as early as possible */
	sock = socket(domain, SOCK_STREAM | extra, 0);
	if (sock < 0) {
		if (errno == EINVAL && extra) {
			/* seems kernel does not accept SOCK_* flags */
			sock_cloexec = 0;
			sock_nonblock = 0;
			extra = 0;
			goto loop;
		}
		return -1;
	}

	if (!sock_cloexec) {
		/* set close fd on exec */
		val = fcntl(sock, F_GETFD, 0);
		if (val == -1)
			return -1;
		res = fcntl(sock, F_SETFD, val | FD_CLOEXEC);
		if (res == -1)
			return -1;
	}

#ifdef SO_NOSIGPIPE
	/* disallow SIGPIPE, if possible */
	val = 1;
	res = setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
	if (res < 0)
		return -1;
#endif

	if (nonblock && !(extra & sock_nonblock))
		if (!socket_set_nonblocking(sock, true))
			return -1;

	return sock;
}

static int listen_socket(const char *addr, int port)
{
	int sock, res, val;
	int af = AF_INET;
	struct sockaddr_in sa4;
	struct in_addr adr4;
	struct sockaddr *sa = (struct sockaddr *)&sa4;

	errno = EINVAL;
	if (inet_pton(AF_INET, addr, &adr4) <= 0)
		return -1;

	sock = make_socket(AF_INET, SOCK_STREAM, 0, true);
	if (sock < 0)
		return -1;

	/* relaxed binding */
	if (af != AF_UNIX) {
		val = 1;
		res = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
		if (res < 0) {
			safe_close(sock);
			return -1;
		}
	}

	sa4.sin_family = AF_INET;
	sa4.sin_addr = adr4;
	sa4.sin_port = htons(port);

	res = bind(sock, sa, sizeof(sa4));
	if (res < 0) {
		safe_close(sock);
		return -1;
	}

	res = listen(sock, 128);
	if (res < 0) {
		safe_close(sock);
		return -1;
	}

	return sock;
}

static void disconnect(struct WClient *conn)
{
	if (conn->fd != -1) {
		safe_close(conn->fd);
		//event_del(&conn->ev);
	}
	free(conn);
}

static const char http_helo[] =
"HTTP/1.0 200 OK\n"
"Server: Apache\n"
"Vary: Accept-Encoding\n"
"Content-Length: 5\n"
"Content-Type: text/html\n"
"Connection: close\n"
"\n"
"HELO\n"
;



static void handle_client(int sock, short flags, void *arg)
{
	struct WClient *conn = arg;
	int avail = sizeof(conn->buf) - conn->bufpos;
	int res;

	res = safe_recv(conn->fd, conn->buf + conn->bufpos, avail, 0);
	if (res < 0) {
		disconnect(conn);
		return;
	}
	printf("%s\n", conn->buf);

	send(conn->fd, http_helo, strlen(http_helo), 0);

	disconnect(conn);
}

static void handle_accept(int srv_sock, short flags, void *arg)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int fd;
	char buf[128];
	struct WClient *conn;

loop:
	memset(&ss, 0, sizeof(ss));
	salen = sizeof(ss);
	fd = safe_accept(srv_sock, sa, &salen);
	if (fd < 0) {
		if (errno == EAGAIN)
			return;
		if (errno == ECONNABORTED)
			return;
		log_error("failed to accept connection");
		return;
	}

	/*
	 * got socket
	 */

	log_info("got client: %s", sa2str(sa, buf, sizeof(buf)));

	conn = calloc(1, sizeof(*conn));
	if (!conn) {
		safe_close(fd);
		return;
	}
	conn->fd = fd;
	event_set(&conn->ev, fd, EV_READ, handle_client, conn);
	if (event_add(&conn->ev, NULL) < 0)
		err(1, "event_add");

	goto loop;
}

int main(int argc, char *argv[])
{
	const char *addr = "127.0.0.1";
	int port = 8000;
	int sock;

	struct event ev_listen;


	if (argc > 1)
		addr = argv[1];
	if (argc > 2)
		port = atoi(argv[2]);

	sock = listen_socket(addr, port);
	if (sock < 0)
		err(1, "listen_socket");


	if (!event_init())
		err(1, "event_init");

	event_set(&ev_listen, sock, EV_READ | EV_PERSIST, handle_accept, NULL);
	if (event_add(&ev_listen, NULL) < 0)
		err(1, "event_add");

	if (event_loop(0) < 0)
		err(1, "event_loop");

	return 0;
}

