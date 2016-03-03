/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_socket_filter.hxx"
#include "filtered_socket.hxx"
#include "pool.hxx"

struct nop_socket_filter {
    FilteredSocket *socket;
};

/*
 * SocketFilter
 *
 */

static void
nop_socket_filter_init(FilteredSocket &s, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket = &s;
}

static BufferedResult
nop_socket_filter_data(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InvokeData(data, length);
}

static bool
nop_socket_filter_is_empty(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalIsEmpty();
}

static bool
nop_socket_filter_is_full(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalIsFull();
}

static size_t
nop_socket_filter_available(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalGetAvailable();
}

static void
nop_socket_filter_consumed(size_t nbytes, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket->InternalConsumed(nbytes);
}

static bool
nop_socket_filter_read(bool expect_more, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalRead(expect_more);
}

static ssize_t
nop_socket_filter_write(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalWrite(data, length);
}

static bool
nop_socket_filter_internal_write(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InvokeWrite();
}

static void
nop_socket_filter_closed(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;
    (void)f;
}

static bool
nop_socket_filter_remaining(size_t remaining, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InvokeRemaining(remaining);
}

static void
nop_socket_filter_end(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket->InvokeEnd();
}

static void
nop_socket_filter_close(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    (void)f;
}

const SocketFilter nop_socket_filter = {
    .init = nop_socket_filter_init,
    .data = nop_socket_filter_data,
    .is_empty = nop_socket_filter_is_empty,
    .is_full = nop_socket_filter_is_full,
    .available = nop_socket_filter_available,
    .consumed = nop_socket_filter_consumed,
    .read = nop_socket_filter_read,
    .write = nop_socket_filter_write,
    .internal_write = nop_socket_filter_internal_write,
    .closed = nop_socket_filter_closed,
    .remaining = nop_socket_filter_remaining,
    .end = nop_socket_filter_end,
    .close = nop_socket_filter_close,
};

/*
 * constructor
 *
 */

void *
nop_socket_filter_new(struct pool &pool)
{
    return NewFromPool<struct nop_socket_filter>(pool);
}
