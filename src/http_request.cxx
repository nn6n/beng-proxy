/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_request.hxx"
#include "http_response.hxx"
#include "http_client.hxx"
#include "http_headers.hxx"
#include "http_address.hxx"
#include "header_writer.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock.hxx"
#include "async.hxx"
#include "growing_buffer.hxx"
#include "lease.hxx"
#include "abort_close.hxx"
#include "failure.hxx"
#include "address_envelope.hxx"
#include "istream.h"
#include "filtered_socket.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <inline/compiler.h>

#include <string.h>

struct http_request {
    struct pool *pool;

    struct tcp_balancer *tcp_balancer;

    unsigned session_sticky;

    const SocketFilter *filter;
    void *filter_ctx;

    struct stock_item *stock_item;
    const struct address_envelope *current_address;

    http_method_t method;
    const struct http_address *uwa;
    HttpHeaders headers;
    struct istream *body;

    unsigned retries;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};

/**
 * Is the specified error a server failure, that justifies
 * blacklisting the server for a while?
 */
static bool
is_server_failure(GError *error)
{
    return error->domain == http_client_quark() &&
        error->code != HTTP_CLIENT_UNSPECIFIED;
}

extern const struct stock_get_handler http_request_stock_handler;

/*
 * HTTP response handler
 *
 */

static void
http_request_response_response(http_status_t status, struct strmap *headers,
                               struct istream *body, void *ctx)
{
    struct http_request *hr = (struct http_request *)ctx;

    failure_unset(&hr->current_address->address,
                  hr->current_address->length,
                  FAILURE_RESPONSE);

    hr->handler.InvokeResponse(status, headers, body);
}

static void
http_request_response_abort(GError *error, void *ctx)
{
    struct http_request *hr = (struct http_request *)ctx;

    if (hr->retries > 0 && hr->body == nullptr &&
        error->domain == http_client_quark() &&
        error->code == HTTP_CLIENT_REFUSED) {
        /* the server has closed the connection prematurely, maybe
           because it didn't want to get any further requests on that
           TCP connection.  Let's try again. */

        g_error_free(error);

        --hr->retries;
        tcp_balancer_get(hr->tcp_balancer, hr->pool,
                         false, SocketAddress::Null(),
                         hr->session_sticky,
                         &hr->uwa->addresses,
                         30,
                         &http_request_stock_handler, hr,
                         hr->async_ref);
    } else {
        if (is_server_failure(error))
            failure_set(&hr->current_address->address,
                        hr->current_address->length,
                        FAILURE_RESPONSE, 20);

        hr->handler.InvokeAbort(error);
    }
}

static const struct http_response_handler http_request_response_handler = {
    .response = http_request_response_response,
    .abort = http_request_response_abort,
};


/*
 * socket lease
 *
 */

static void
http_socket_release(bool reuse, void *ctx)
{
    struct http_request *hr = (struct http_request *)ctx;

    tcp_balancer_put(hr->tcp_balancer, hr->stock_item, !reuse);
}

static const struct lease http_socket_lease = {
    .release = http_socket_release,
};


/*
 * stock callback
 *
 */

static void
http_request_stock_ready(struct stock_item *item, void *ctx)
{
    struct http_request *hr = (struct http_request *)ctx;

    hr->stock_item = item;
    hr->current_address = tcp_balancer_get_last();

    http_client_request(*hr->pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        http_socket_lease, hr,
                        hr->filter, hr->filter_ctx,
                        hr->method, hr->uwa->path, std::move(hr->headers),
                        hr->body, true,
                        http_request_response_handler, hr,
                        *hr->async_ref);
}

static void
http_request_stock_error(GError *error, void *ctx)
{
    struct http_request *hr = (struct http_request *)ctx;

    if (hr->body != nullptr)
        istream_close_unused(hr->body);

    if (hr->filter != nullptr)
        hr->filter->close(hr->filter_ctx);

    hr->handler.InvokeAbort(error);
}

const struct stock_get_handler http_request_stock_handler = {
    .ready = http_request_stock_ready,
    .error = http_request_stock_error,
};


/*
 * constructor
 *
 */

void
http_request(struct pool &pool,
             struct tcp_balancer &tcp_balancer,
             unsigned session_sticky,
             const SocketFilter *filter, void *filter_ctx,
             http_method_t method,
             const struct http_address &uwa,
             HttpHeaders &&headers,
             struct istream *body,
             const struct http_response_handler &handler,
             void *handler_ctx,
             struct async_operation_ref &_async_ref)
{
    assert(uwa.host_and_port != nullptr);
    assert(uwa.path != nullptr);
    assert(handler.response != nullptr);
    assert(body == nullptr || !istream_has_handler(body));

    auto hr = NewFromPool<struct http_request>(pool);
    hr->pool = &pool;
    hr->tcp_balancer = &tcp_balancer;
    hr->session_sticky = session_sticky;
    hr->filter = filter;
    hr->filter_ctx = filter_ctx;
    hr->method = method;
    hr->uwa = &uwa;

    hr->headers = std::move(headers);

    hr->handler.Set(handler, handler_ctx);
    hr->async_ref = &_async_ref;

    struct async_operation_ref *async_ref = &_async_ref;
    if (body != nullptr) {
        body = istream_hold_new(&pool, body);
        async_ref = &async_close_on_abort(pool, *body, *async_ref);
    }

    hr->body = body;

    growing_buffer &headers2 = hr->headers.MakeBuffer(pool, 256);
    if (uwa.host_and_port != nullptr)
        header_write(&headers2, "host", uwa.host_and_port);

    header_write(&headers2, "connection", "keep-alive");

    hr->retries = 2;
    tcp_balancer_get(&tcp_balancer, &pool,
                     false, SocketAddress::Null(),
                     session_sticky,
                     &uwa.addresses,
                     30,
                     &http_request_stock_handler, hr,
                     async_ref);
}
