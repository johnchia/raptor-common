/*
 * rss_net.h — Network utility helpers for RSS daemons
 *
 * Address formatting, socket setup, listen socket creation.
 * All static inline — no additional .c file needed.
 */

#ifndef RSS_NET_H
#define RSS_NET_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Format sockaddr_storage to string. IPv4-mapped IPv6 shown as plain IPv4. */
static inline const char *rss_addr_str(const struct sockaddr_storage *ss, char *buf, size_t bufsz)
{
    const char *r = NULL;
    if (ss->ss_family == AF_INET) {
        r = inet_ntop(AF_INET, &((const struct sockaddr_in *)ss)->sin_addr, buf, bufsz);
    } else if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)ss;
        if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr))
            r = inet_ntop(AF_INET, &s6->sin6_addr.s6_addr[12], buf, bufsz);
        else
            r = inet_ntop(AF_INET6, &s6->sin6_addr, buf, bufsz);
    }
    if (!r)
        snprintf(buf, bufsz, "???");
    return buf;
}

/* Extract port from sockaddr_storage (host byte order). */
static inline uint16_t rss_addr_port(const struct sockaddr_storage *ss)
{
    if (ss->ss_family == AF_INET)
        return ntohs(((const struct sockaddr_in *)ss)->sin_port);
    if (ss->ss_family == AF_INET6)
        return ntohs(((const struct sockaddr_in6 *)ss)->sin6_port);
    return 0;
}

/* Set fd to non-blocking mode. Returns 0 on success, -1 on error. */
static inline int rss_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * Create a TCP socket on the widest address family the kernel offers.
 *
 * IPv6 is first-class in this project: it is 2026 and every kernel is
 * expected to have it, so IPv6 always gets priority and the fallback below
 * exists for the few that truly do not -- never as a preference.
 *
 * A dual-stack AF_INET6 socket with IPV6_V6ONLY cleared is preferred, since
 * one fd then serves clients of both families. But a kernel built without
 * IPv6 refuses AF_INET6 outright with EAFNOSUPPORT, and those are real
 * targets -- OpenIPC ships IPv6-less kernels on the smaller SoCs -- so fall
 * back to AF_INET rather than failing. The fallback serves IPv4 only, which
 * is the whole of what such a kernel can route anyway.
 *
 * Only the two family-specific errnos trigger the fallback; anything else
 * (EMFILE, ENOMEM) is reported as-is instead of being masked by a second
 * failure with a different cause.
 *
 * Returns the fd and stores the family actually obtained in *family, which
 * the caller needs in order to build the matching sockaddr for bind().
 */
static inline int rss_socket_tcp(int *family)
{
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd >= 0) {
        int zero = 0;
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
        if (family)
            *family = AF_INET6;
        return fd;
    }
    if (errno != EAFNOSUPPORT && errno != EPROTONOSUPPORT)
        return -1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (family)
        *family = AF_INET;
    return fd;
}

/*
 * The UDP twin of rss_socket_tcp: same family preference, same fallback
 * rule, same *family out-parameter for building the bind address.
 */
static inline int rss_socket_udp(int *family)
{
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd >= 0) {
        int zero = 0;
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
        if (family)
            *family = AF_INET6;
        return fd;
    }
    if (errno != EAFNOSUPPORT && errno != EPROTONOSUPPORT)
        return -1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    if (family)
        *family = AF_INET;
    return fd;
}

/*
 * Fill *ss with the wildcard bind address for `family`. Returns the length
 * to hand bind(), or 0 if the family is one neither arm below knows.
 */
static inline socklen_t rss_sockaddr_any(int family, uint16_t port, struct sockaddr_storage *ss)
{
    memset(ss, 0, sizeof(*ss));

    if (family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons(port);
        a6->sin6_addr = in6addr_any;
        return (socklen_t)sizeof(*a6);
    }
    if (family == AF_INET) {
        struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
        a4->sin_family = AF_INET;
        a4->sin_port = htons(port);
        a4->sin_addr.s_addr = htonl(INADDR_ANY);
        return (socklen_t)sizeof(*a4);
    }
    return 0;
}

/*
 * Create a TCP listen socket on the given port, dual-stack where the kernel
 * supports IPv6 and IPv4-only where it does not.
 * Sets SO_REUSEADDR and binds to the wildcard address.
 * Returns fd on success, -1 on error.
 */
static inline int rss_listen_tcp(int port, int backlog)
{
    int family = AF_INET6;
    int fd = rss_socket_tcp(&family);
    if (fd < 0)
        return -1;

    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_storage addr;
    socklen_t addrlen = rss_sockaddr_any(family, (uint16_t)port, &addr);

    if (addrlen == 0 || bind(fd, (struct sockaddr *)&addr, addrlen) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, backlog) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * As above, bound to one address rather than the wildcard.
 *
 * A camera in setup mode answers on the access point it raised and nowhere
 * else. Binding the wildcard there would put the same unauthenticated daemon
 * on every interface the camera happens to have -- which on a camera being set
 * up is the one network nobody has chosen yet.
 *
 * IPv4 only, deliberately: the address is one the camera assigned itself on a
 * link it is the only router for, and there is no IPv6 equivalent of that
 * situation here. An empty address is the wildcard, so a caller with nothing
 * to restrict to does not need a second branch.
 */
static inline int rss_listen_tcp_addr(const char *addr, int port, int backlog)
{
    if (!addr || !addr[0])
        return rss_listen_tcp(port, backlog);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in a4;
    memset(&a4, 0, sizeof(a4));
    a4.sin_family = AF_INET;
    a4.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, addr, &a4.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&a4, sizeof(a4)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, backlog) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Set TCP_NODELAY on a socket. */
static inline void rss_set_tcp_nodelay(int fd)
{
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

#ifdef __cplusplus
}
#endif

#endif /* RSS_NET_H */
