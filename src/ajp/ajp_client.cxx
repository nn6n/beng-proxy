/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_client.hxx"
#include "ajp_headers.hxx"
#include "ajp_serialize.hxx"
#include "ajp_protocol.hxx"
#include "buffered_socket.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "growing_buffer.hxx"
#include "pevent.hxx"
#include "format.h"
#include "istream_ajp_body.hxx"
#include "istream_gb.hxx"
#include "istream/istream_internal.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_memory.hxx"
#include "serialize.hxx"
#include "please.hxx"
#include "uri/uri_verify.hxx"
#include "direct.hxx"
#include "strmap.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"
#include "pool.hxx"

#include <daemon/log.h>
#include <socket/util.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <limits.h>

struct AjpClient {
    /* I/O */
    BufferedSocket socket;
    struct lease_ref lease_ref;

    /* request */
    struct {
        struct istream *istream;

        /** an istream_ajp_body */
        struct istream *ajp_body;

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;

        struct http_response_handler_ref handler;
    } request;

    struct async_operation request_async;

    /* response */
    struct Response {
        enum {
            READ_BEGIN,

            /**
             * The #AJP_CODE_SEND_HEADERS indicates that there is no
             * response body.  Waiting for the #AJP_CODE_END_RESPONSE
             * packet, and then we'll forward the response to the
             * #http_response_handler.
             */
            READ_NO_BODY,

            READ_BODY,
            READ_END,
        } read_state;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        bool no_body;

        /**
         * This flag is true if ConsumeSendHeaders() is currently
         * calling the HTTP response handler.  During this period,
         * istream_ajp_read() does nothing, to prevent recursion.
         */
        bool in_handler;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        http_status_t status;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        struct strmap *headers;

        size_t chunk_length, junk_length;

        /**
         * The remaining response body, -1 if unknown.
         */
        off_t remaining;
    } response;

    struct istream response_body;

    AjpClient(struct pool &p, int fd, FdType fd_type,
              Lease &lease);

    struct pool &GetPool() {
        return *response_body.pool;
    }

    void ScheduleWrite() {
        socket.ScheduleWrite();
    }

    /**
     * Release the AJP connection socket.
     */
    void ReleaseSocket(bool reuse);

    /**
     * Release resources held by this object: the event object, the
     * socket lease, the request body and the pool reference.
     */
    void Release(bool reuse);

    void AbortResponseHeaders(GError *error);
    void AbortResponseBody(GError *error);
    void AbortResponse(GError *error);

    void AbortResponseHeaders(const char *msg) {
        AbortResponseHeaders(g_error_new_literal(ajp_client_quark(), 0, msg));
    }

    void AbortResponse(const char *msg) {
        AbortResponse(g_error_new_literal(ajp_client_quark(), 0, msg));
    }

    /**
     * @return false if the #AjpClient has been closed
     */
    bool ConsumeSendHeaders(const uint8_t *data, size_t length);

    /**
     * @return false if the #AjpClient has been closed
     */
    bool ConsumePacket(enum ajp_code code, const uint8_t *data, size_t length);

    /**
     * Consume response body chunk data.
     *
     * @return the number of bytes consumed
     */
    size_t ConsumeBodyChunk(const void *data, size_t length);

    /**
     * Discard junk data after a response body chunk.
     *
     * @return the number of bytes consumed
     */
    size_t ConsumeBodyJunk(size_t length);

    /**
     * Handle the remaining data in the input buffer.
     */
    BufferedResult Feed(const uint8_t *data, const size_t length);

    void Abort();
};

static const struct timeval ajp_client_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

static const struct ajp_header empty_body_chunk = {
    .a = 0x12, .b = 0x34,
};

void
AjpClient::ReleaseSocket(bool reuse)
{
    assert(socket.IsConnected());
    assert(response.read_state == Response::READ_BODY ||
           response.read_state == Response::READ_END);

    socket.Abandon();
    p_lease_release(lease_ref, reuse, GetPool());
}

void
AjpClient::Release(bool reuse)
{
    assert(socket.IsValid());
    assert(response.read_state == Response::READ_END);

    if (socket.IsConnected())
        ReleaseSocket(reuse);

    socket.Destroy();

    if (request.istream != nullptr)
        istream_free_handler(&request.istream);

    istream_deinit(&response_body);
}

void
AjpClient::AbortResponseHeaders(GError *error)
{
    assert(response.read_state == Response::READ_BEGIN ||
           response.read_state == Response::READ_NO_BODY);

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    response.read_state = Response::READ_END;
    request_async.Finished();
    request.handler.InvokeAbort(error);

    Release(false);
}

void
AjpClient::AbortResponseBody(GError *error)
{
    assert(response.read_state == Response::READ_BODY);

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    response.read_state = Response::READ_END;
    istream_invoke_abort(&response_body, error);

    Release(false);
}

void
AjpClient::AbortResponse(GError *error)
{
    assert(response.read_state != Response::READ_END);

    switch (response.read_state) {
    case Response::READ_BEGIN:
    case Response::READ_NO_BODY:
        AbortResponseHeaders(error);
        break;

    case Response::READ_BODY:
        AbortResponseBody(error);
        break;

    case Response::READ_END:
        assert(false);
        break;
    }
}


/*
 * response body stream
 *
 */

static inline AjpClient *
istream_to_ajp(struct istream *istream)
{
    return &ContainerCast2(*istream, &AjpClient::response_body);
}

static off_t
istream_ajp_available(struct istream *istream, bool partial)
{
    AjpClient *client = istream_to_ajp(istream);

    assert(client->response.read_state == AjpClient::Response::READ_BODY);

    if (client->response.remaining >= 0)
        /* the Content-Length was announced by the AJP server */
        return client->response.remaining;

    if (partial)
        /* we only know how much is left in the current chunk */
        return client->response.chunk_length;

    /* no clue */
    return -1;
}

static void
istream_ajp_read(struct istream *istream)
{
    AjpClient *client = istream_to_ajp(istream);

    assert(client->response.read_state == AjpClient::Response::READ_BODY);

    if (client->response.in_handler)
        return;

    client->socket.Read(true);
}

static void
istream_ajp_close(struct istream *istream)
{
    AjpClient *client = istream_to_ajp(istream);

    assert(client->response.read_state == AjpClient::Response::READ_BODY);

    client->response.read_state = AjpClient::Response::READ_END;

    client->Release(false);
}

static const struct istream_class ajp_response_body = {
    .available = istream_ajp_available,
    .skip = nullptr,
    .read = istream_ajp_read,
    .as_fd = nullptr,
    .close = istream_ajp_close,
};


/*
 * response parser
 *
 */

inline bool
AjpClient::ConsumeSendHeaders(const uint8_t *data, size_t length)
{
    unsigned num_headers;
    struct strmap *headers;

    if (response.read_state != Response::READ_BEGIN) {
        GError *error =
            g_error_new(ajp_client_quark(), 0,
                        "unexpected SEND_HEADERS packet from AJP server");
        AbortResponseBody(error);
        return false;
    }

    ConstBuffer<void> packet(data, length);
    http_status_t status = (http_status_t)deserialize_uint16(packet);
    deserialize_ajp_string(packet);
    num_headers = deserialize_uint16(packet);

    if (num_headers > 0) {
        headers = strmap_new(&GetPool());
        deserialize_ajp_response_headers(&GetPool(), headers,
                                         packet, num_headers);
    } else
        headers = nullptr;

    if (packet.IsNull()) {
        AbortResponseHeaders("malformed SEND_HEADERS packet from AJP server");
        return false;
    }

    if (!http_status_is_valid(status)) {
        GError *error =
            g_error_new(ajp_client_quark(), 0,
                        "invalid status %u from AJP server", status);
        AbortResponseHeaders(error);
        return false;
    }

    if (response.no_body || http_status_is_empty(status)) {
        response.read_state = Response::READ_NO_BODY;
        response.status = status;
        response.headers = headers;
        response.chunk_length = 0;
        response.junk_length = 0;
        return true;
    }

    const char *content_length = strmap_remove_checked(headers, "content-length");
    if (content_length != nullptr) {
        char *endptr;
        response.remaining = strtoul(content_length, &endptr, 10);
        if (endptr == content_length || *endptr != 0) {
            AbortResponseHeaders("Malformed Content-Length from AJP server");
            return false;
        }
    } else
        response.remaining = -1;

    response.read_state = Response::READ_BODY;
    response.chunk_length = 0;
    response.junk_length = 0;

    request_async.Finished();

    response.in_handler = true;
    request.handler.InvokeResponse(status, headers, &response_body);
    response.in_handler = false;

    return socket.IsValid();
}

inline bool
AjpClient::ConsumePacket(enum ajp_code code,
                         const uint8_t *data, size_t length)
{
    const struct ajp_get_body_chunk *chunk;

    switch (code) {
    case AJP_CODE_FORWARD_REQUEST:
    case AJP_CODE_SHUTDOWN:
    case AJP_CODE_CPING:
        AbortResponse("unexpected request packet from AJP server");
        return false;

    case AJP_CODE_SEND_BODY_CHUNK:
        assert(0); /* already handled in Feed() */
        return false;

    case AJP_CODE_SEND_HEADERS:
        return ConsumeSendHeaders(data, length);

    case AJP_CODE_END_RESPONSE:
        if (response.read_state == Response::READ_BODY) {
            if (response.remaining > 0) {
                AbortResponse("premature end of response AJP server");
                return false;
            }

            response.read_state = Response::READ_END;
            istream_invoke_eof(&response_body);
            Release(true);
        } else if (response.read_state == Response::READ_NO_BODY) {
            response.read_state = Response::READ_END;
            Release(socket.IsEmpty());

            request.handler.InvokeResponse(response.status,
                                           response.headers,
                                           nullptr);
        } else
            Release(true);

        return false;

    case AJP_CODE_GET_BODY_CHUNK:
        chunk = (const struct ajp_get_body_chunk *)(data - 1);

        if (length < sizeof(*chunk) - 1) {
            AbortResponse("malformed AJP GET_BODY_CHUNK packet");
            return false;
        }

        if (request.istream == nullptr ||
            request.ajp_body == nullptr) {
            /* we always send empty_body_chunk to the AJP server, so
               we can safely ignore all other AJP_CODE_GET_BODY_CHUNK
               requests here */
            return true;
        }

        istream_ajp_body_request(request.ajp_body,
                                 FromBE16(chunk->length));
        ScheduleWrite();
        return true;

    case AJP_CODE_CPONG_REPLY:
        /* XXX */
        break;
    }

    AbortResponse("unknown packet from AJP server");
    return false;
}

inline size_t
AjpClient::ConsumeBodyChunk(const void *data, size_t length)
{
    assert(response.read_state == Response::READ_BODY);
    assert(response.chunk_length > 0);
    assert(data != nullptr);
    assert(length > 0);

    if (length > response.chunk_length)
        length = response.chunk_length;

    size_t nbytes = istream_invoke_data(&response_body, data, length);
    if (nbytes > 0) {
        response.chunk_length -= nbytes;
        response.remaining -= nbytes;
    }

    return nbytes;
}

inline size_t
AjpClient::ConsumeBodyJunk(size_t length)
{
    assert(response.read_state == Response::READ_BODY ||
           response.read_state == Response::READ_NO_BODY);
    assert(response.chunk_length == 0);
    assert(response.junk_length > 0);
    assert(length > 0);

    if (length > response.junk_length)
        length = response.junk_length;

    response.junk_length -= length;
    return length;
}

inline BufferedResult
AjpClient::Feed(const uint8_t *data, const size_t length)
{
    assert(response.read_state == Response::READ_BEGIN ||
           response.read_state == Response::READ_NO_BODY ||
           response.read_state == Response::READ_BODY);
    assert(data != nullptr);
    assert(length > 0);

    const uint8_t *const end = data + length;

    do {
        if (response.read_state == Response::READ_BODY ||
            response.read_state == Response::READ_NO_BODY) {
            /* there is data left from the previous body chunk */
            if (response.chunk_length > 0) {
                const size_t remaining = end - data;
                size_t nbytes = ConsumeBodyChunk(data, remaining);
                if (nbytes == 0)
                    return socket.IsValid()
                        ? BufferedResult::BLOCKING
                        : BufferedResult::CLOSED;

                data += nbytes;
                socket.Consumed(nbytes);
                if (data == end || response.chunk_length > 0)
                    /* want more data */
                    return nbytes < remaining
                        ? BufferedResult::PARTIAL
                        : BufferedResult::MORE;
            }

            if (response.junk_length > 0) {
                size_t nbytes = ConsumeBodyJunk(end - data);
                assert(nbytes > 0);

                data += nbytes;
                socket.Consumed(nbytes);
                if (data == end || response.chunk_length > 0)
                    /* want more data */
                    return BufferedResult::MORE;
            }
        }

        if (data + sizeof(struct ajp_header) + 1 > end)
            /* we need a full header */
            return BufferedResult::MORE;

        const struct ajp_header *header = (const struct ajp_header*)data;
        size_t header_length = FromBE16(header->length);

        if (header->a != 'A' || header->b != 'B' || header_length == 0) {
            AbortResponse("malformed AJP response packet");
            return BufferedResult::CLOSED;
        }

        const ajp_code code = (ajp_code)data[sizeof(*header)];

        if (code == AJP_CODE_SEND_BODY_CHUNK) {
            const struct ajp_send_body_chunk *chunk =
                (const struct ajp_send_body_chunk *)(header + 1);

            if (response.read_state != Response::READ_BODY &&
                response.read_state != Response::READ_NO_BODY) {
                AbortResponse("unexpected SEND_BODY_CHUNK packet from AJP server");
                return BufferedResult::CLOSED;
            }

            const size_t nbytes = sizeof(*header) + sizeof(*chunk);
            if (data + nbytes > end)
                /* we need the chunk length */
                return BufferedResult::MORE;

            response.chunk_length = FromBE16(chunk->length);
            if (sizeof(*chunk) + response.chunk_length > header_length) {
                AbortResponse("malformed AJP SEND_BODY_CHUNK packet");
                return BufferedResult::CLOSED;
            }

            response.junk_length = header_length - sizeof(*chunk) - response.chunk_length;

            if (response.read_state == AjpClient::Response::READ_NO_BODY) {
                /* discard all response body chunks after HEAD request */
                response.junk_length += response.chunk_length;
                response.chunk_length = 0;
            }

            if (response.remaining >= 0 &&
                (off_t)response.chunk_length > response.remaining) {
                AbortResponse("excess chunk length in AJP SEND_BODY_CHUNK packet");
                return BufferedResult::CLOSED;
            }

            /* consume the body chunk header and start sending the
               body */
            socket.Consumed(nbytes);
            data += nbytes;
            continue;
        }

        const size_t nbytes = sizeof(*header) + header_length;

        if (data + nbytes > end)
            /* the packet is not complete yet */
            return BufferedResult::MORE;

        socket.Consumed(nbytes);

        if (!ConsumePacket(code, data + sizeof(*header) + 1,
                           header_length - 1))
            return BufferedResult::CLOSED;

        data += nbytes;
    } while (data != end);

    return BufferedResult::MORE;
}

/*
 * istream handler for the request
 *
 */

static size_t
ajp_request_stream_data(const void *data, size_t length, void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    assert(client != nullptr);
    assert(client->socket.IsConnected());
    assert(client->request.istream != nullptr);
    assert(data != nullptr);
    assert(length > 0);

    client->request.got_data = true;

    ssize_t nbytes = client->socket.Write(data, length);
    if (likely(nbytes >= 0)) {
        client->ScheduleWrite();
        return (size_t)nbytes;
    }

    if (likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED))
        return 0;

    GError *error =
        g_error_new(ajp_client_quark(), 0,
                    "write error on AJP client connection: %s",
                    strerror(errno));
    client->AbortResponse(error);
    return 0;
}

static ssize_t
ajp_request_stream_direct(FdType type, int fd, size_t max_length,
                          void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    assert(client != nullptr);
    assert(client->socket.IsConnected());
    assert(client->request.istream != nullptr);

    client->request.got_data = true;

    ssize_t nbytes = client->socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0))
        client->ScheduleWrite();
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;
    else if (nbytes < 0 && errno == EAGAIN) {
        client->request.got_data = false;
        client->socket.UnscheduleWrite();
    }

    return nbytes;
}

static void
ajp_request_stream_eof(void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    assert(client->request.istream != nullptr);

    client->request.istream = nullptr;

    client->socket.UnscheduleWrite();
    client->socket.Read(true);
}

static void
ajp_request_stream_abort(GError *error, void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    assert(client->request.istream != nullptr);

    client->request.istream = nullptr;

    if (client->response.read_state == AjpClient::Response::READ_END)
        /* this is a recursive call, this object is currently being
           destructed further up the stack */
        return;

    g_prefix_error(&error, "AJP request stream failed: ");
    client->AbortResponse(error);
}

static const struct istream_handler ajp_request_stream_handler = {
    .data = ajp_request_stream_data,
    .direct = ajp_request_stream_direct,
    .eof = ajp_request_stream_eof,
    .abort = ajp_request_stream_abort,
};

/*
 * socket_wrapper handler
 *
 */

static BufferedResult
ajp_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    const ScopePoolRef ref(client->GetPool() TRACE_ARGS);
    return client->Feed((const uint8_t *)buffer, size);
}

static bool
ajp_client_socket_closed(void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    /* the rest of the response may already be in the input buffer */
    client->ReleaseSocket(false);
    return true;
}

static bool
ajp_client_socket_remaining(gcc_unused size_t remaining, void *ctx)
{
    gcc_unused
    AjpClient *client = (AjpClient *)ctx;

    /* only READ_BODY could have blocked */
    assert(client->response.read_state == AjpClient::Response::READ_BODY);

    /* the rest of the response may already be in the input buffer */
    return true;
}

static bool
ajp_client_socket_write(void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    const ScopePoolRef ref(client->GetPool() TRACE_ARGS);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = client->socket.IsValid() &&
        client->socket.IsConnected();
    if (result && client->request.istream != nullptr) {
        if (client->request.got_data)
            client->ScheduleWrite();
        else
            client->socket.UnscheduleWrite();
    }

    return result;
}

static void
ajp_client_socket_error(GError *error, void *ctx)
{
    AjpClient *client = (AjpClient *)ctx;

    g_prefix_error(&error, "AJP connection failed: ");
    client->AbortResponse(error);
}

static constexpr BufferedSocketHandler ajp_client_socket_handler = {
    .data = ajp_client_socket_data,
    .direct = nullptr,
    .closed = ajp_client_socket_closed,
    .remaining = ajp_client_socket_remaining,
    .end = nullptr,
    .write = ajp_client_socket_write,
    .drained = nullptr,
    .timeout = nullptr,
    .broken = nullptr,
    .error = ajp_client_socket_error,
};

inline void
AjpClient::Abort()
{
    /* async_operation_ref::Abort() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == Response::READ_BEGIN ||
           response.read_state == Response::READ_NO_BODY);

    response.read_state = Response::READ_END;
    Release(false);
}

/*
 * constructor
 *
 */

inline
AjpClient::AjpClient(struct pool &p, int fd, FdType fd_type,
                     Lease &lease)
{
    socket.Init(p, fd, fd_type,
                &ajp_client_timeout, &ajp_client_timeout,
                ajp_client_socket_handler, this);

    istream_init(&response_body, &ajp_response_body, &p);

    p_lease_ref_set(lease_ref, lease,
                    p, "ajp_client_lease");
}

void
ajp_client_request(struct pool *pool, int fd, FdType fd_type,
                   Lease &lease,
                   const char *protocol, const char *remote_addr,
                   const char *remote_host, const char *server_name,
                   unsigned server_port, bool is_ssl,
                   http_method_t method, const char *uri,
                   struct strmap *headers,
                   struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    assert(protocol != nullptr);
    assert(http_method_is_valid(method));

    if (!uri_path_verify_quick(uri)) {
        lease.ReleaseLease(true);
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new(ajp_client_quark(), 0,
                        "malformed request URI '%s'", uri);
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    auto client = NewFromPool<AjpClient>(*pool, *pool,
                                         fd, fd_type,
                                         lease);

    GrowingBuffer *gb = growing_buffer_new(pool, 256);

    struct ajp_header *header = (struct ajp_header *)
        growing_buffer_write(gb, sizeof(*header));
    header->a = 0x12;
    header->b = 0x34;

    const enum ajp_method ajp_method = to_ajp_method(method);
    if (ajp_method == AJP_METHOD_NULL) {
        /* invalid or unknown method */
        p_lease_release(client->lease_ref, true, client->GetPool());
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new_literal(ajp_client_quark(), 0,
                                "unknown request method");
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    struct {
        uint8_t prefix_code, method;
    } prefix_and_method;
    prefix_and_method.prefix_code = AJP_CODE_FORWARD_REQUEST;
    prefix_and_method.method = (uint8_t)ajp_method;

    growing_buffer_write_buffer(gb, &prefix_and_method, sizeof(prefix_and_method));

    const char *query_string = strchr(uri, '?');
    size_t uri_length = query_string != nullptr
        ? (size_t)(query_string - uri)
        : strlen(uri);

    serialize_ajp_string(gb, protocol);
    serialize_ajp_string_n(gb, uri, uri_length);
    serialize_ajp_string(gb, remote_addr);
    serialize_ajp_string(gb, remote_host);
    serialize_ajp_string(gb, server_name);
    serialize_ajp_integer(gb, server_port);
    serialize_ajp_bool(gb, is_ssl);

    GrowingBuffer *headers_buffer = nullptr;
    unsigned num_headers = 0;
    if (headers != nullptr) {
        /* serialize the request headers - note that
           serialize_ajp_headers() ignores the Content-Length header,
           we will append it later */
        headers_buffer = growing_buffer_new(pool, 2048);
        num_headers = serialize_ajp_headers(headers_buffer, headers);
    }

    /* Content-Length */

    if (body != nullptr)
        ++num_headers;

    serialize_ajp_integer(gb, num_headers);
    if (headers != nullptr)
        growing_buffer_cat(gb, headers_buffer);

    off_t available = -1;

    size_t requested;
    if (body != nullptr) {
        available = istream_available(body, false);
        if (available == -1) {
            /* AJPv13 does not support chunked request bodies */
            p_lease_release(client->lease_ref, true, client->GetPool());
            istream_close_unused(body);

            GError *error =
                g_error_new_literal(ajp_client_quark(), 0,
                                    "AJPv13 does not support chunked request bodies");
            handler->InvokeAbort(handler_ctx, error);
            return;
        }

        if (available == 0)
            istream_free_unused(&body);
        else
            requested = 1024;
    }

    if (available >= 0) {
        char buffer[32];
        format_uint64(buffer, (uint64_t)available);
        serialize_ajp_integer(gb, AJP_HEADER_CONTENT_LENGTH);
        serialize_ajp_string(gb, buffer);
    }

    /* attributes */

    if (query_string != nullptr) {
        char name = AJP_ATTRIBUTE_QUERY_STRING;
        growing_buffer_write_buffer(gb, &name, sizeof(name));
        serialize_ajp_string(gb, query_string + 1); /* skip the '?' */
    }

    growing_buffer_write_buffer(gb, "\xff", 1);

    /* XXX is this correct? */

    header->length = ToBE16(growing_buffer_size(gb) - sizeof(*header));

    struct istream *request = istream_gb_new(pool, gb);
    if (body != nullptr) {
        client->request.ajp_body = istream_ajp_body_new(pool, body);
        istream_ajp_body_request(client->request.ajp_body, requested);
        request = istream_cat_new(pool, request, client->request.ajp_body,
                                  istream_memory_new(pool, &empty_body_chunk,
                                                     sizeof(empty_body_chunk)),
                                  nullptr);
    } else {
        client->request.ajp_body = nullptr;
    }

    istream_assign_handler(&client->request.istream, request,
                           &ajp_request_stream_handler, client,
                           client->socket.GetDirectMask());

    client->request.handler.Set(*handler, handler_ctx);

    client->request_async.Init2<AjpClient, &AjpClient::request_async>();
    async_ref->Set(client->request_async);

    /* XXX append request body */

    client->response.read_state = AjpClient::Response::READ_BEGIN;
    client->response.no_body = http_method_is_empty(method);
    client->response.in_handler = false;

    client->socket.ScheduleReadNoTimeout(true);
    istream_read(client->request.istream);
}
