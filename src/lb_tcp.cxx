/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_tcp.hxx"
#include "filtered_socket.hxx"
#include "address_list.hxx"
#include "client_balancer.hxx"
#include "address_sticky.h"
#include "async.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"

#include <unistd.h>
#include <errno.h>

struct lb_tcp {
    struct pool *pool;
    Stock *pipe_stock;

    const struct lb_tcp_handler *handler;
    void *handler_ctx;

    FilteredSocket inbound;

    BufferedSocket outbound;

    struct async_operation_ref connect;

    bool got_inbound_data, got_outbound_data;
};

static constexpr timeval write_timeout = { 30, 0 };

static void
lb_tcp_destroy_inbound(struct lb_tcp *tcp)
{
    if (tcp->inbound.IsConnected())
        tcp->inbound.Close();

    tcp->inbound.Destroy();
}

static void
lb_tcp_destroy_outbound(struct lb_tcp *tcp)
{
    if (tcp->outbound.IsConnected())
        tcp->outbound.Close();

    tcp->outbound.Destroy();
}

/*
 * inbound BufferedSocketHandler
 *
 */

static BufferedResult
inbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->got_inbound_data = true;

    if (tcp->connect.IsDefined())
        /* outbound is not yet connected */
        return BufferedResult::BLOCKING;

    if (!tcp->outbound.IsValid()) {
        lb_tcp_close(tcp);
        tcp->handler->error("Send error", "Broken socket", tcp->handler_ctx);
        return BufferedResult::CLOSED;
    }

    ssize_t nbytes = tcp->outbound.Write(buffer, size);
    if (nbytes > 0) {
        tcp->inbound.Consumed(nbytes);
        return (size_t)nbytes == size
            ? BufferedResult::OK
            : BufferedResult::PARTIAL;
    }

    switch ((enum write_result)nbytes) {
    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        lb_tcp_close(tcp);
        tcp->handler->_errno("Send failed", errno, tcp->handler_ctx);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
inbound_buffered_socket_closed(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return false;
}

static bool
inbound_buffered_socket_write(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->got_outbound_data = false;

    if (!tcp->outbound.Read(false))
        return false;

    if (!tcp->got_outbound_data)
        tcp->inbound.UnscheduleWrite();
    return true;
}

static bool
inbound_buffered_socket_drained(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    if (!tcp->outbound.IsValid()) {
        /* now that inbound's output buffers are drained, we can
           finally close the connection (postponed from
           outbound_buffered_socket_end()) */
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return false;
    }

    return true;
}

static enum write_result
inbound_buffered_socket_broken(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return WRITE_DESTROYED;
}

static void
inbound_buffered_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->gerror("Error", error, tcp->handler_ctx);
}

static constexpr BufferedSocketHandler inbound_buffered_socket_handler = {
    inbound_buffered_socket_data,
    nullptr, // TODO: inbound_buffered_socket_direct,
    inbound_buffered_socket_closed,
    nullptr,
    nullptr,
    inbound_buffered_socket_write,
    inbound_buffered_socket_drained,
    nullptr,
    inbound_buffered_socket_broken,
    inbound_buffered_socket_error,
};

/*
 * outbound buffered_socket_handler
 *
 */

static BufferedResult
outbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->got_outbound_data = true;

    ssize_t nbytes = tcp->inbound.Write(buffer, size);
    if (nbytes > 0) {
        tcp->outbound.Consumed(nbytes);
        return (size_t)nbytes == size
            ? BufferedResult::OK
            : BufferedResult::PARTIAL;
    }

    switch ((enum write_result)nbytes) {
        int save_errno;

    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        save_errno = errno;
        lb_tcp_close(tcp);
        tcp->handler->_errno("Send failed", save_errno, tcp->handler_ctx);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
outbound_buffered_socket_closed(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->outbound.Close();
    return true;
}

static void
outbound_buffered_socket_end(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->outbound.Destroy();

    tcp->inbound.UnscheduleWrite();

    if (tcp->inbound.IsDrained()) {
        /* all output buffers to "inbound" are drained; close the
           connection, because there's nothing left to do */
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);

        /* nothing will be done if the buffers are not yet drained;
           we're waiting for inbound_buffered_socket_drained() to be
           called */
    }
}

static bool
outbound_buffered_socket_write(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->got_inbound_data = false;

    if (!tcp->inbound.Read(false))
        return false;

    if (!tcp->got_inbound_data)
        tcp->outbound.UnscheduleWrite();
    return true;
}

static enum write_result
outbound_buffered_socket_broken(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return WRITE_DESTROYED;
}

static void
outbound_buffered_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->gerror("Error", error, tcp->handler_ctx);
}

static constexpr BufferedSocketHandler outbound_buffered_socket_handler = {
    outbound_buffered_socket_data,
    nullptr, // TODO: outbound_buffered_socket_direct,
    outbound_buffered_socket_closed,
    nullptr,
    outbound_buffered_socket_end,
    outbound_buffered_socket_write,
    nullptr,
    nullptr,
    outbound_buffered_socket_broken,
    outbound_buffered_socket_error,
};

/*
 * stock_handler
 *
 */

static void
lb_tcp_client_socket_success(SocketDescriptor &&fd, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->connect.Clear();

    tcp->outbound.Init(*tcp->pool,
                       fd.Steal(), FdType::FD_TCP,
                       nullptr, &write_timeout,
                       outbound_buffered_socket_handler, tcp);

    /* TODO
    tcp->outbound.direct = tcp->pipe_stock != nullptr &&
        (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0 &&
        (istream_direct_mask_to(tcp->inbound.base.base.fd_type) & FdType::FD_PIPE) != 0;
    */

    if (tcp->inbound.Read(false))
        tcp->outbound.Read(false);
}

static void
lb_tcp_client_socket_timeout(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_destroy_inbound(tcp);
    tcp->handler->error("Connect error", "Timeout", tcp->handler_ctx);
}

static void
lb_tcp_client_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_destroy_inbound(tcp);
    tcp->handler->gerror("Connect error", error, tcp->handler_ctx);
}

static constexpr ConnectSocketHandler lb_tcp_client_socket_handler = {
    .success = lb_tcp_client_socket_success,
    .timeout = lb_tcp_client_socket_timeout,
    .error = lb_tcp_client_socket_error,
};

/*
 * constructor
 *
 */

gcc_pure
static unsigned
lb_tcp_sticky(const AddressList &address_list,
              const struct sockaddr *remote_address)
{
    switch (address_list.sticky_mode) {
    case STICKY_NONE:
    case STICKY_FAILOVER:
        break;

    case STICKY_SOURCE_IP:
        return socket_address_sticky(remote_address);

    case STICKY_SESSION_MODULO:
    case STICKY_COOKIE:
    case STICKY_JVM_ROUTE:
        /* not implemented here */
        break;
    }

    return 0;
}

void
lb_tcp_new(struct pool *pool, Stock *pipe_stock,
           SocketDescriptor &&fd, FdType fd_type,
           const SocketFilter *filter, void *filter_ctx,
           SocketAddress remote_address,
           bool transparent_source,
           const AddressList &address_list,
           struct balancer &balancer,
           const struct lb_tcp_handler *handler, void *ctx,
           lb_tcp **tcp_r)
{
    lb_tcp *tcp = NewFromPool<lb_tcp>(*pool);
    tcp->pool = pool;
    tcp->pipe_stock = pipe_stock;
    tcp->handler = handler;
    tcp->handler_ctx = ctx;

    tcp->inbound.Init(*pool, fd.Steal(), fd_type,
                      nullptr, &write_timeout,
                      filter, filter_ctx,
                      inbound_buffered_socket_handler, tcp);
    /* TODO
    tcp->inbound.base.direct = pipe_stock != nullptr &&
        (ISTREAM_TO_PIPE & fd_type) != 0 &&
        (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0;
    */

    unsigned session_sticky = lb_tcp_sticky(address_list,
                                            remote_address.GetAddress());

    SocketAddress bind_address = SocketAddress::Null();

    if (transparent_source) {
        bind_address = remote_address;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address.GetFamily() == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        } else if (bind_address.GetFamily() == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin6_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        }
    }

    *tcp_r = tcp;

    client_balancer_connect(pool, &balancer,
                            transparent_source,
                            bind_address,
                            session_sticky,
                            &address_list,
                            20,
                            &lb_tcp_client_socket_handler, tcp,
                            &tcp->connect);
}

void
lb_tcp_close(struct lb_tcp *tcp)
{
    if (tcp->inbound.IsValid())
        lb_tcp_destroy_inbound(tcp);

    if (tcp->connect.IsDefined())
        tcp->connect.Abort();
    else if (tcp->outbound.IsValid())
        lb_tcp_destroy_outbound(tcp);
}
