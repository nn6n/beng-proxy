/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INSTANCE_HXX
#define BENG_PROXY_INSTANCE_HXX

#include "config.hxx"
#include "shutdown_listener.h"
#include "bp_listener.hxx"
#include "event/Event.hxx"
#include "event/DelayedTrigger.hxx"
#include "control_handler.hxx"

#include <inline/list.h>

#include <forward_list>

#include <event.h>

struct Stock;
struct StockMap;
struct TcpBalancer;
class ControlDistribute;
struct ControlServer;
struct LocalControl;
struct TranslateStock;
struct LhttpStock;
struct FcgiStock;
struct NfsCache;
class HttpCache;
class FilterCache;

struct instance final : ControlHandler {
    struct pool *pool;

    struct config config;

    EventBase event_base;

    uint64_t http_request_counter;

    std::forward_list<BPListener> listeners;

    struct list_head connections;
    unsigned num_connections;

    bool should_exit;
    struct shutdown_listener shutdown_listener;
    struct event sighup_event;

    /* child management */
    DelayedTrigger respawn_trigger;
    struct list_head workers;
    unsigned num_workers;

    /**
     * This object distributes all control packets received by the
     * master process to all worker processes.
     */
    ControlDistribute *control_distribute;

    /**
     * The configured control channel server (see --control-listen),
     * nullptr if none was configured.
     */
    ControlServer *control_server;

    /**
     * The implicit per-process control server.  It listens on a local
     * socket "@beng-proxy:PID" and will accept connections only from
     * root or the beng-proxy user.
     */
    LocalControl *local_control_server;

    /* stock */
    TranslateStock *translate_stock;
    struct tcache *translate_cache;
    struct balancer *balancer;
    StockMap *tcp_stock;
    TcpBalancer *tcp_balancer;
    struct memcached_stock *memcached_stock;

    /* cache */
    HttpCache *http_cache;

    FilterCache *filter_cache;

    LhttpStock *lhttp_stock;
    FcgiStock *fcgi_stock;

    StockMap *was_stock;

    StockMap *delegate_stock;

    struct nfs_stock *nfs_stock;
    NfsCache *nfs_cache;

    Stock *pipe_stock;

    struct resource_loader *resource_loader;

    instance();

    void ForkCow(bool inherit);

    /**
     * Handler for #CONTROL_FADE_CHILDREN
     */
    void FadeChildren();

    /* virtual methods from class ControlHandler */
    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(Error &&error) override;

private:
    void RespawnWorkerCallback();
};

struct client_connection;

void
init_signals(struct instance *instance);

void
deinit_signals(struct instance *instance);

void
all_listeners_event_add(struct instance *instance);

void
all_listeners_event_del(struct instance *instance);

#endif
