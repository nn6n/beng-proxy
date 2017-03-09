/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_TCP_H
#define BENG_PROXY_LB_TCP_H

#include "FdType.hxx"

#include <exception>

struct pool;
class EventLoop;
class Stock;
struct SocketFilter;
struct AddressList;
struct Balancer;
struct LbClusterConfig;
struct LbTcpConnection;
class UniqueSocketDescriptor;
class SocketAddress;
class LbClusterMap;

struct LbTcpConnectionHandler {
    void (*eof)(void *ctx);
    void (*error)(const char *prefix, const char *error, void *ctx);
    void (*_errno)(const char *prefix, int error, void *ctx);
    void (*exception)(const char *prefix, std::exception_ptr ep, void *ctx);
};

/**
 * @param transparent_source see #lb_cluster_config::transparent_source
 */
void
lb_tcp_new(struct pool &pool, EventLoop &event_loop, Stock *pipe_stock,
           UniqueSocketDescriptor &&fd, FdType fd_type,
           const SocketFilter *filter, void *filter_ctx,
           SocketAddress remote_address,
           const LbClusterConfig &cluster,
           LbClusterMap &clusters,
           Balancer &balancer,
           const LbTcpConnectionHandler &handler, void *ctx,
           LbTcpConnection **tcp_r);

void
lb_tcp_close(LbTcpConnection *tcp);

#endif
