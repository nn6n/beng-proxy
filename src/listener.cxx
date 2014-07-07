/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.hxx"
#include "fd_util.h"
#include "pool.h"
#include "util/Error.hxx"

#include <socket/util.h>
#include <socket/address.h>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <event.h>

struct listener {
    const int fd;
    struct event event;

    const struct listener_handler &handler;
    void *handler_ctx;

    listener(int _fd,
             const struct listener_handler &_handler, void *_handler_ctx)
        :fd(_fd), handler(_handler), handler_ctx(_handler_ctx) {}

    ~listener() {
        close(fd);
    }
};

static void
listener_event_callback(int fd, short event gcc_unused, void *ctx)
{
    struct listener *listener = (struct listener *)ctx;
    struct sockaddr_storage sa;
    size_t sa_len;
    int remote_fd;

    sa_len = sizeof(sa);
    remote_fd = accept_cloexec_nonblock(fd, (struct sockaddr*)&sa, &sa_len);
    if (remote_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Error error;
            error.SetErrno("accept() failed");
            listener->handler.error(std::move(error), listener->handler_ctx);
        }

        return;
    }

    if (!socket_set_nodelay(remote_fd, true)) {
        Error error;
        error.SetErrno("setsockopt(TCP_NODELAY) failed");

        close(remote_fd);
        listener->handler.error(std::move(error), listener->handler_ctx);
        return;
    }

    listener->handler.connected(remote_fd,
                                (const struct sockaddr*)&sa, sa_len,
                                listener->handler_ctx);

    pool_commit();
}

static gcc_always_inline uint16_t
my_htons(uint16_t x)
{
#ifdef __ICC
#ifdef __LITTLE_ENDIAN
    /* icc seriously doesn't like the htons() macro */
    return (uint16_t)((x >> 8) | (x << 8));
#else
    return x;
#endif
#else
    return (uint16_t)htons((uint16_t)x);
#endif
}

struct listener *
listener_new(int family, int socktype, int protocol,
             const struct sockaddr *address, size_t address_length,
             const struct listener_handler *handler, void *ctx,
             Error &error)
{
    int ret, param;

    assert(address != nullptr);
    assert(address_length > 0);
    assert(handler != nullptr);
    assert(handler->connected != nullptr);
    assert(handler->error != nullptr);

    int fd = socket_cloexec_nonblock(family, socktype, protocol);
    if (fd < 0) {
        error.SetErrno("Failed to create socket");
        return nullptr;
    }

    auto listener = new struct listener(fd, *handler, ctx);

    param = 1;
    ret = setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
    if (ret < 0) {
        error.SetErrno("Failed to configure SO_REUSEADDR");
        delete listener;
        return nullptr;
    }

    if (address->sa_family == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)address;
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);
    }

    ret = bind(listener->fd, address, address_length);
    if (ret < 0) {
        char buffer[64];
        socket_address_to_string(buffer, sizeof(buffer),
                                 address, address_length);
        error.FormatErrno("Failed to bind to '%s'", buffer);
        delete listener;
        return nullptr;
    }

#ifdef __linux
    /* enable TCP Fast Open (requires Linux 3.7) */

#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN 23
#endif

    if ((family == AF_INET || family == AF_INET6) &&
        socktype == SOCK_STREAM) {
        int qlen = 16;
        setsockopt(listener->fd, SOL_TCP, TCP_FASTOPEN,
                   &qlen, sizeof(qlen));
    }
#endif

    ret = listen(listener->fd, 64);
    if (ret < 0) {
        error.SetErrno("Failed to listen");
        delete listener;
        return nullptr;
    }

    event_set(&listener->event, listener->fd,
              EV_READ|EV_PERSIST, listener_event_callback, listener);

    listener_event_add(listener);

    return listener;
}

struct listener *
listener_tcp_port_new(int port,
                      const struct listener_handler *handler, void *ctx,
                      Error &error)
{
    struct listener *listener;
    struct sockaddr_in6 sa6;
    struct sockaddr_in sa4;

    assert(port > 0);
    assert(handler != nullptr);
    assert(handler->connected != nullptr);
    assert(handler->error != nullptr);

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = my_htons((uint16_t)port);

    listener = listener_new(PF_INET6, SOCK_STREAM, 0,
                            (const struct sockaddr *)&sa6, sizeof(sa6),
                            handler, ctx, IgnoreError());
    if (listener != nullptr)
        return listener;

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = my_htons((uint16_t)port);

    return listener_new(PF_INET, SOCK_STREAM, 0,
                        (const struct sockaddr *)&sa4, sizeof(sa4),
                        handler, ctx, error);
}

void
listener_free(struct listener **listener_r)
{
    struct listener *listener = *listener_r;
    *listener_r = nullptr;

    assert(listener != nullptr);
    assert(listener->fd >= 0);

    listener_event_del(listener);
    delete listener;
}

void
listener_event_add(struct listener *listener)
{
    event_add(&listener->event, nullptr);
}

void
listener_event_del(struct listener *listener)
{
    event_del(&listener->event);
}
