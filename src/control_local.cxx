/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_local.hxx"
#include "control_server.hxx"
#include "net/SocketAddress.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>

struct LocalControl {
    const char *prefix;

    const struct control_handler *handler;
    void *handler_ctx;

    ControlServer *server;
};

/*
 * control_handler
 *
 */

static bool
control_local_raw(const void *data, size_t length,
                  SocketAddress address,
                  int uid, void *ctx)
{
    LocalControl *cl = (LocalControl *)ctx;

    if (uid < 0 || (uid != 0 && (uid_t)uid != geteuid()))
        /* only root and the beng-proxy user are allowed to send
           commands to the implicit control channel */
        return false;

    return cl->handler->raw == nullptr ||
        cl->handler->raw(data, length, address,
                         uid, cl->handler_ctx);
}

static void
control_local_packet(ControlServer &control_server,
                     enum beng_control_command command,
                     const void *payload, size_t payload_length,
                     SocketAddress address,
                     void *ctx)
{
    LocalControl *cl = (LocalControl *)ctx;

    cl->handler->packet(control_server, command, payload, payload_length,
                        address,
                        cl->handler_ctx);
}

static void
control_local_error(GError *error, void *ctx)
{
    LocalControl *cl = (LocalControl *)ctx;

    cl->handler->error(error, cl->handler_ctx);
}

static const struct control_handler control_local_handler = {
    .raw = control_local_raw,
    .packet = control_local_packet,
    .error = control_local_error,
};

/*
 * public
 *
 */

LocalControl *
control_local_new(const char *prefix,
                  const struct control_handler *handler, void *ctx)
{
    auto cl = new LocalControl();
    cl->prefix = prefix;
    cl->handler = handler;
    cl->handler_ctx = ctx;
    cl->server = nullptr;

    return cl;
}

static void
control_local_close(LocalControl *cl)
{
    delete cl->server;
    cl->server = nullptr;
}

void
control_local_free(LocalControl *cl)
{
    control_local_close(cl);
    delete cl;
}

bool
control_local_open(LocalControl *cl, GError **error_r)
{
    control_local_close(cl);

    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    sa.sun_path[0] = '\0';
    sprintf(sa.sun_path + 1, "%s%d", cl->prefix, (int)getpid());

    cl->server = new ControlServer(&control_local_handler, cl);
    if (!cl->server->Open(SocketAddress((const struct sockaddr *)&sa,
                                        SUN_LEN(&sa) + 1 + strlen(sa.sun_path + 1)),
                          error_r)) {
        control_local_close(cl);
        return false;
    }

    return true;
}

ControlServer *
control_local_get(LocalControl *cl)
{
    assert(cl != nullptr);
    assert(cl->server != nullptr);

    return cl->server;
}
