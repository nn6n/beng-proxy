#define ENABLE_PREMATURE_CLOSE_HEADERS
#define ENABLE_PREMATURE_CLOSE_BODY

#include "tio.hxx"
#include "t_client.hxx"
#include "ajp/ajp_client.hxx"
#include "ajp/ajp_protocol.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "fd-util.h"
#include "growing_buffer.hxx"
#include "header_writer.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "fd_util.h"
#include "istream.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "util/ByteOrder.hxx"

#include <inline/compiler.h>

#include <sys/wait.h>

struct connection {
    pid_t pid;
    int fd;
};

static void
client_request(struct pool *pool, struct connection *connection,
               const struct lease *lease, void *lease_ctx,
               http_method_t method, const char *uri,
               struct strmap *headers,
               struct istream *body,
               const struct http_response_handler *handler,
               void *ctx,
               struct async_operation_ref *async_ref)
{
    ajp_client_request(pool, connection->fd, ISTREAM_SOCKET,
                       lease, lease_ctx,
                       "http", "192.168.1.100", "remote", "server", 80, false,
                       method, uri, headers, body,
                       handler, ctx, async_ref);
}

static void
connection_close(struct connection *c)
{
    assert(c != nullptr);
    assert(c->pid >= 1);
    assert(c->fd >= 0);

    close(c->fd);
    c->fd = -1;

    int status;
    if (waitpid(c->pid, &status, 0) < 0) {
        perror("waitpid() failed");
        exit(EXIT_FAILURE);
    }

    assert(!WIFSIGNALED(status));
}

struct ajp_request {
    enum ajp_code code;
    enum ajp_method method;
    const char *uri;
    struct strmap *headers;

    uint8_t *body;
    size_t length, requested, received;
};

static char *
read_string_n(struct pool *pool, size_t length, size_t *remaining_r)
{
    if (length == 0xffff)
        return nullptr;

    if (*remaining_r < length + 1)
        exit(EXIT_FAILURE);

    char *value = (char *)p_malloc(pool, length + 1);
    read_full(value, length + 1);
    if (value[length] != 0)
        exit(EXIT_FAILURE);

    *remaining_r -= length + 1;
    return value;
}

static char *
read_string(struct pool *pool, size_t *remaining_r)
{
    const size_t length = read_short(remaining_r);
    return read_string_n(pool, length, remaining_r);
}

static void
read_ajp_header(struct ajp_header *header)
{
    read_full(header, sizeof(*header));
    if (header->a != 0x12 || header->b != 0x34)
        exit(EXIT_FAILURE);
}

static void
write_string(const char *value)
{
    if (value != nullptr) {
        size_t length = strlen(value);
        if (length > 0xfffe)
            length = 0xfffe;

        write_short(length);
        write_full(value, length);
        write_byte(0);
    } else
        write_short(0xffff);
}

static void
write_get_body_chunk(size_t length)
{
    assert(length <= 0xffff);

    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(3),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_GET_BODY_CHUNK);
    write_short(length);
}

static void
read_ajp_request(struct pool *pool, struct ajp_request *r)
{
    struct ajp_header header;
    read_ajp_header(&header);

    size_t remaining = FromBE16(header.length);

    r->code = (ajp_code)read_byte(&remaining);
    if (r->code != AJP_CODE_FORWARD_REQUEST) {
        discard(remaining);
        return;
    }

    r->method = (ajp_method)read_byte(&remaining);

    read_string(pool, &remaining); /* protocol */
    r->uri = read_string(pool, &remaining);
    read_string(pool, &remaining); /* remote_address */
    read_string(pool, &remaining); /* remote_host */
    read_string(pool, &remaining); /* server_name */
    read_short(&remaining); /* server_port */
    read_byte(&remaining); /* is_ssl */

    r->headers = strmap_new(pool);

    unsigned n_headers = read_short(&remaining);
    while (n_headers-- > 0) {
        unsigned name_length = read_short(&remaining);
        const ajp_header_code code = (ajp_header_code)name_length;
        const char *name = ajp_decode_header_name(code);
        if (name == nullptr) {
            char *name2 = read_string_n(pool, name_length, &remaining);
            if (name2 == nullptr)
                exit(EXIT_FAILURE);

            name = p_strndup_lower(pool, name2, name_length);
        }

        const char *value = read_string(pool, &remaining);
        r->headers->Add(name, value);
    }

    // ...

    discard(remaining);

    const char *length_string = r->headers->Get("content-length");
    r->length = length_string != nullptr
        ? strtoul(length_string, nullptr, 10)
        : 0;
    r->body = r->length > 0
        ? (uint8_t *)p_malloc(pool, r->length)
        : nullptr;
    r->requested = 0;
    r->received = 0;
}

static void
read_ajp_request_body_chunk(struct ajp_request *r)
{
    assert(r->length > 0);
    assert(r->received < r->length);
    assert(r->body != nullptr);

    const size_t remaining = r->length - r->received;

    while (r->requested <= r->received) {
        size_t nbytes = remaining;
        if (nbytes > 8192)
            nbytes = 8192;

        write_get_body_chunk(nbytes);
        r->requested += nbytes;
    }

    struct ajp_header header;
    read_ajp_header(&header);

    size_t packet_length = FromBE16(header.length);
    size_t chunk_length = read_short(&packet_length);
    if (chunk_length == 0 || chunk_length > packet_length ||
        chunk_length > remaining)
        exit(EXIT_FAILURE);

    read_full(r->body + r->received, chunk_length);
    r->received += chunk_length;

    size_t junk_length = packet_length - chunk_length;
    discard(junk_length);
}

static void
read_ajp_end_request_body_chunk(struct ajp_request *r)
{
    assert(r->length > 0);
    assert(r->received == r->length);
    assert(r->body != nullptr);

    struct ajp_header header;
    read_ajp_header(&header);
    size_t packet_length = FromBE16(header.length);
    if (packet_length == 0)
        return;

    size_t chunk_length = read_short(&packet_length);
    if (chunk_length != 0)
        exit(EXIT_FAILURE);
}

/*
static void
discard_ajp_request_body(struct ajp_request *r)
{
    if (r->length == 0)
        return;

    while (r->received < r->length)
        read_ajp_request_body_chunk(r);

    read_ajp_end_request_body_chunk(r);
}
*/

static void
write_headers(http_status_t status, const struct strmap *headers)
{
    unsigned n = 0;
    size_t length = 7;

    if (headers != nullptr) {
        for (const auto &i : *headers) {
            ++n;
            length += 4;

            enum ajp_response_header_code code =
                ajp_encode_response_header_name(i.key);
            if (code == AJP_RESPONSE_HEADER_NONE)
                length += strlen(i.key) + 1;

            length += strlen(i.value) + 1;
        }
    }

    const struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(length),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_SEND_HEADERS);
    write_short(status);
    write_string(nullptr);

    write_short(n);

    if (headers != nullptr) {
        for (const auto &i : *headers) {
            enum ajp_response_header_code code =
                ajp_encode_response_header_name(i.key);
            if (code == AJP_RESPONSE_HEADER_NONE)
                write_string(i.key);
            else
                write_short(code);

            write_string(i.value);
        }
    }
}

static void
write_body_chunk(const void *value, size_t length, size_t junk)
{
    assert(length + junk <= 0xffff);

    const struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(3 + length + junk),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_SEND_BODY_CHUNK);
    write_short(length);
    write_full(value, length);
    fill(junk);
}

static void
write_end(void)
{
    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(1),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_END_RESPONSE);
}

static struct connection *
connect_server(void (*f)(struct pool *pool))
{
    int sv[2];
    pid_t pid;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        dup2(sv[1], 0);
        dup2(sv[1], 1);
        close(sv[0]);
        close(sv[1]);

        struct pool *pool = pool_new_libc(nullptr, "f");
        f(pool);
        shutdown(0, SHUT_RDWR);
        pool_unref(pool);
        exit(EXIT_SUCCESS);
    }

    close(sv[1]);

    fd_set_nonblock(sv[0], 1);

    static struct connection c;
    c.pid = pid;
    c.fd = sv[0];
    return &c;
}

static void
ajp_server_null(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    write_headers(HTTP_STATUS_NO_CONTENT, nullptr);
    write_end();
}

static struct connection *
connect_null(void)
{
    return connect_server(ajp_server_null);
}

static void
ajp_server_hello(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    write_headers(HTTP_STATUS_OK, nullptr);
    write_body_chunk("hello", 5, 0);
    write_end();
}

static struct connection *
connect_hello(void)
{
    return connect_server(ajp_server_hello);
}

static struct connection *
connect_dummy(void)
{
    return connect_hello();
}

static struct connection *
connect_fixed(void)
{
    return connect_hello();
}

static void
ajp_server_tiny(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    struct strmap *headers = strmap_new(pool);
    headers->Add("content-length", "5");

    write_headers(HTTP_STATUS_OK, headers);
    write_body_chunk("hello", 5, 0);
    write_end();
}

static struct connection *
connect_tiny(void)
{
    return connect_server(ajp_server_tiny);
}

static void
ajp_server_mirror(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    if (request.code != AJP_CODE_FORWARD_REQUEST)
        exit(EXIT_FAILURE);

    http_status_t status = request.length == 0
        ? HTTP_STATUS_NO_CONTENT
        : HTTP_STATUS_OK;

    write_headers(status, request.headers);

    if (request.method != AJP_METHOD_HEAD) {
        size_t position = 0;
        while (position < request.length) {
            if (request.received < request.length && position == request.received)
                read_ajp_request_body_chunk(&request);

            assert(position < request.received);

            size_t nbytes = request.received - position;
            if (nbytes > 8192)
                nbytes = 8192;

            write_body_chunk(request.body + position, nbytes, 0);
            position += nbytes;
        }

        if (request.length > 0)
            read_ajp_end_request_body_chunk(&request);
    }

    write_end();
}

static struct connection *
connect_mirror(void)
{
    return connect_server(ajp_server_mirror);
}

static void
ajp_server_hold(struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);
    write_headers(HTTP_STATUS_OK, nullptr);

    /* wait until the connection gets closed */
    struct ajp_header header;
    read_ajp_header(&header);
}

static struct connection *
connect_hold(void)
{
    return connect_server(ajp_server_hold);
}

static void
ajp_server_premature_close_headers(gcc_unused struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(256),
    };

    write_full(&header, sizeof(header));
}

static struct connection *
connect_premature_close_headers(void)
{
    return connect_server(ajp_server_premature_close_headers);
}

static void
ajp_server_premature_close_body(gcc_unused struct pool *pool)
{
    struct ajp_request request;
    read_ajp_request(pool, &request);

    write_headers(HTTP_STATUS_OK, nullptr);

    static constexpr struct ajp_header header = {
        .a = 'A',
        .b = 'B',
        .length = ToBE16(256),
    };

    write_full(&header, sizeof(header));
    write_byte(AJP_CODE_SEND_BODY_CHUNK);
    write_short(200);
}

static struct connection *
connect_premature_close_body(void)
{
    return connect_server(ajp_server_premature_close_body);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    event_base = event_init();
    fb_pool_init(false);

    pool = pool_new_libc(nullptr, "root");

    run_all_tests(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();

    int status;
    while (wait(&status) > 0) {
        assert(!WIFSIGNALED(status));
    }
}
