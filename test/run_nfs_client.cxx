#include "nfs_client.hxx"
#include "istream_nfs.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "system/SetupProcess.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "util/Cancellable.hxx"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct Context final : NfsClientHandler {
    EventLoop event_loop;

    struct pool *pool;

    const char *path;

    ShutdownListener shutdown_listener;
    CancellablePointer cancel_ptr;

    NfsClient *client;

    bool aborted = false, failed = false, connected = false, closed = false;

    SinkFd *body;
    bool body_eof = false, body_abort = false, body_closed = false;

    Context()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

    void ShutdownCallback();

    /* virtual methods from NfsClientHandler */
    void OnNfsClientReady(NfsClient &client) override;
    void OnNfsMountError(GError *error) override;
    void OnNfsClientClosed(GError *error) override;
};

void
Context::ShutdownCallback()
{
    aborted = true;

    if (body != nullptr)
        sink_fd_close(body);
    else
        cancel_ptr.Cancel();
}

/*
 * sink_fd handler
 *
 */

static void
my_sink_fd_input_eof(void *ctx)
{
    Context *c = (Context *)ctx;

    c->body = nullptr;
    c->body_eof = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static void
my_sink_fd_input_error(GError *error, void *ctx)
{
    Context *c = (Context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    Context *c = (Context *)ctx;

    fprintf(stderr, "%s\n", strerror(error));

    sink_fd_close(c->body);

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
    return false;
}

static constexpr SinkFdHandler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};

/*
 * NfsClientOpenFileHandler
 *
 */

static void
my_open_ready(NfsFileHandle *handle, const struct stat *st, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(c->connected);

    Istream *body = istream_nfs_new(*c->pool, *handle, 0, st->st_size);
    body = istream_pipe_new(c->pool, *body, nullptr);
    c->body = sink_fd_new(*c->pool, *body, 1, guess_fd_type(1),
                          my_sink_fd_handler, ctx);
    body->Read();
}

static void
my_open_error(GError *error, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(c->connected);

    c->failed = true;

    g_printerr("open error: %s\n", error->message);
    g_error_free(error);

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static constexpr NfsClientOpenFileHandler my_open_handler = {
    .ready = my_open_ready,
    .error = my_open_error,
};

/*
 * nfs_client_handler
 *
 */

void
Context::OnNfsClientReady(NfsClient &_client)
{
    assert(!aborted);
    assert(!failed);
    assert(!connected);
    assert(!closed);

    connected = true;
    client = &_client;

    nfs_client_open_file(client, pool, path,
                         &my_open_handler, this,
                         cancel_ptr);
}

void
Context::OnNfsMountError(GError *error)
{
    assert(!aborted);
    assert(!failed);
    assert(!connected);
    assert(!closed);

    failed = true;

    g_printerr("mount error: %s\n", error->message);
    g_error_free(error);

    shutdown_listener.Disable();
}

void
Context::OnNfsClientClosed(GError *error)
{
    assert(!aborted);
    assert(!failed);
    assert(connected);
    assert(!closed);

    closed = true;

    g_printerr("closed: %s\n", error->message);
    g_error_free(error);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    if (argc != 4) {
        g_printerr("usage: run_nfs_client SERVER ROOT PATH\n");
        return EXIT_FAILURE;
    }

    const char *const server = argv[1];
    const char *const _export = argv[2];

    Context ctx;
    ctx.path = argv[3];

    /* initialize */

    SetupProcess();

    direct_global_init();

    ctx.shutdown_listener.Enable();

    RootPool root_pool;
    ctx.pool = pool_new_libc(root_pool, "pool");

    /* open NFS connection */

    nfs_client_new(ctx.event_loop, *ctx.pool, server, _export,
                   ctx, ctx.cancel_ptr);
    pool_unref(ctx.pool);

    /* run */

    ctx.event_loop.Dispatch();

    assert(ctx.aborted || ctx.failed || ctx.connected);

    /* cleanup */

    return ctx.connected
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
