#include "esp-mgba.h"

#include <lwip/sockets.h>

#include "mobile.h"

/* 
 * Mostly taken from https://github.com/REONTeam/libmobile-bgb/
 */

#define socket_close(x) close(x)
#define socket_seterror(x) (errno = (x))
#define socket_geterror() (errno)
#define socket_perror(x) ESP_LOGI("socket", "perror - " x)

#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#define SOCKET_EAGAIN EAGAIN
#define SOCKET_EINPROGRESS EINPROGRESS
#define SOCKET_EALREADY EALREADY

static int sockets[MOBILE_MAX_CONNECTIONS] = {-1, -1};

static int socket_hasdata(int sock, int delay)
{
    int count = 0;
    ioctl(sock, FIONREAD, &count);
    return count;
}

static int socket_setblocking(int socket, int flag)
{
    int flags = fcntl(socket, F_GETFL);
    if (flags == -1)
        return -1;

    flags &= ~O_NONBLOCK;
    if (!flag) flags |= O_NONBLOCK;
    if (fcntl(socket, F_SETFL, flags) == -1)
        return -1;

    return 0;
}

int socket_isconnected(int socket, int delay)
{
    struct pollfd fd = {
        .fd = socket,
        .events = POLLOUT,
    };
    int rc = poll(&fd, 1, delay);
    if (rc == -1) socket_perror("poll");
    if (rc <= 0) return rc;

    int err;
    socklen_t err_len = sizeof(err);
    getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&err, &err_len);
    socket_seterror(err);
    if (err) return -1;
    return 1;
}

union u_sockaddr {
    struct sockaddr addr;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
};

static struct sockaddr *convert_sockaddr(socklen_t *addrlen, union u_sockaddr *u_addr, const struct mobile_addr *addr)
{
    *addrlen = 0;
    struct sockaddr *res = NULL;
    if (!addr) return res;
    if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        memset(&u_addr->addr4, 0, sizeof(u_addr->addr4));
        u_addr->addr4.sin_family = AF_INET;
        u_addr->addr4.sin_port = htons(addr4->port);
        if (sizeof(struct in_addr) != sizeof(addr4->host)) return res;
        memcpy(&u_addr->addr4.sin_addr.s_addr, addr4->host,
            sizeof(struct in_addr));
        *addrlen = sizeof(struct sockaddr_in);
        res = &u_addr->addr;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        memset(&u_addr->addr6, 0, sizeof(u_addr->addr6));
        u_addr->addr6.sin6_family = AF_INET6;
        u_addr->addr6.sin6_port = htons(addr6->port);
        if (sizeof(struct in6_addr) != sizeof(addr6->host)) return res;
        memcpy(&u_addr->addr6.sin6_addr.s6_addr, addr6->host,
            sizeof(struct in6_addr));
        *addrlen = sizeof(struct sockaddr_in6);
        res = &u_addr->addr;
    }
    return res;
}

bool mobile_board_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport)
{
    assert(sockets[conn] == -1);

    int sock_socktype;
    switch (socktype) {
        case MOBILE_SOCKTYPE_TCP: sock_socktype = SOCK_STREAM; break;
        case MOBILE_SOCKTYPE_UDP: sock_socktype = SOCK_DGRAM; break;
        default: return false;
    }

    int sock_addrtype;
    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4: sock_addrtype = AF_INET; break;
        case MOBILE_ADDRTYPE_IPV6: sock_addrtype = AF_INET6; break;
        default: return false;
    }

    int sock = socket(sock_addrtype, sock_socktype, 0);
    if (sock == -1) {
        socket_perror("socket");
        return false;
    }
    if (socket_setblocking(sock, 0) == -1) return false;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
            (char *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        socket_close(sock);
        return false;
    }

    int rc = -1;
    if (addrtype == MOBILE_ADDRTYPE_IPV4) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(bindport),
        };
        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } else {
        struct sockaddr_in6 addr = {
            .sin6_family = AF_INET6,
            .sin6_port = htons(bindport),
        };
        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (rc == -1) {
        socket_perror("bind");
        socket_close(sock);
        return false;
    }

    sockets[conn] = sock;
    return true;
}

void mobile_board_sock_close(void *user, unsigned conn)
{
    assert(sockets[conn] != -1);
    socket_close(sockets[conn]);
    sockets[conn] = -1;
}

int mobile_board_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr)
{
    int sock = sockets[conn];

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    // Try to connect/check if we're connected
    if (connect(sock, sock_addr, sock_addrlen) != -1) return 1;
    int err = socket_geterror();

    // If the connection is in progress, block at most 100ms to see if it's
    //   enough for it to connect.
    if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EINPROGRESS
            || err == SOCKET_EALREADY) {
        int rc = socket_isconnected(sock, 100);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        err = socket_geterror();
    }

    char sock_host[INET6_ADDRSTRLEN] = {0};
    char sock_port[6] = {0};
    //socket_straddr(sock_host, sizeof(sock_host), sock_port, sock_addr,
    //    sock_addrlen);
    //socket_seterror(err);
    //fprintf(stderr, "Could not connect (ip %s port %s):",
    //    sock_host, sock_port);
    //socket_perror(NULL);
    return -1;
}

bool mobile_board_sock_listen(void *user, unsigned conn)
{
    int sock = sockets[conn];

    if (listen(sock, 1) == -1) {
        socket_perror("listen");
        return false;
    }

    return true;
}

bool mobile_board_sock_accept(void *user, unsigned conn)
{
    int sock = sockets[conn];

    if (socket_hasdata(sock, 1000) <= 0) return false;
    int newsock = accept(sock, NULL, NULL);
    if (newsock == -1) {
        socket_perror("accept");
        return false;
    }
    if (socket_setblocking(newsock, 0) == -1) return false;

    socket_close(sock);
    sockets[conn] = newsock;
    return true;
}

int mobile_board_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr)
{
    int sock = sockets[conn];

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    ssize_t len = sendto(sock, data, size, 0, sock_addr, sock_addrlen);
    if (len == -1) {
        // If the socket is blocking, we just haven't sent anything
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EAGAIN) return 0;

        socket_perror("send");
        return -1;
    }
    return len;
}

int mobile_board_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    int sock = sockets[conn];

    // Make sure at least one byte is in the buffer
    // BUG - Not working on esp?
    //if (socket_hasdata(sock, 0) <= 0) return 0;

    union u_sockaddr u_addr = {0};
    socklen_t sock_addrlen = sizeof(u_addr);
    struct sockaddr *sock_addr = (struct sockaddr *)&u_addr;

    ssize_t len = 0;
    if (data) {
        // Retrieve at least 1 byte from the buffer
        len = recvfrom(sock, data, size, 0, sock_addr, &sock_addrlen);
    } else {
        // Check if at least 1 byte is available in buffer
        char c;
        len = recvfrom(sock, &c, 1, MSG_PEEK, sock_addr, &sock_addrlen);
    }
    if (len == -1) {
        // If the socket is blocking, we just haven't received anything
        // Though this shouldn't happen thanks to the socket_hasdata check.
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EAGAIN) return 0;

        socket_perror("recv");
        return -1;
    }

    // A length of 0 will be returned if the remote has disconnected.
    if (len == 0) {
        // Though it's only relevant to TCP sockets, as UDP sockets may receive
        // zero-length datagrams.
        int sock_type = 0;
        socklen_t sock_type_len = sizeof(sock_type);
        getsockopt(sock, SOL_SOCKET, SO_TYPE, (char *)&sock_type,
            &sock_type_len);
        if (sock_type == SOCK_STREAM) return -2;
    }

    if (!data) return 0;

    if (addr && sock_addrlen) {
        if (sock_addr->sa_family == AF_INET) {
            struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            addr4->type = MOBILE_ADDRTYPE_IPV4;
            addr4->port = ntohs(u_addr.addr4.sin_port);
            memcpy(addr4->host, &u_addr.addr4.sin_addr.s_addr,
                sizeof(addr4->host));
        } else if (sock_addr->sa_family == AF_INET6) {
            struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            addr6->type = MOBILE_ADDRTYPE_IPV6;
            addr6->port = ntohs(u_addr.addr6.sin6_port);
            memcpy(addr6->host, &u_addr.addr6.sin6_addr.s6_addr,
                sizeof(addr6->host));
        }
    }

    return len;
}
