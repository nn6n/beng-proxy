/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-loader.h"
#include "resource-address.h"
#include "http-cache.h"
#include "http-request.h"
#include "http-response.h"
#include "static-file.h"
#include "cgi.h"
#include "fcgi-request.h"
#include "was-glue.h"
#include "ajp-request.h"
#include "header-writer.h"
#include "pipe.h"
#include "delegate-request.h"

#include <string.h>
#include <stdlib.h>

struct resource_loader {
    struct hstock *tcp_stock;
    struct hstock *fcgi_stock;
    struct hstock *was_stock;
    struct hstock *delegate_stock;
};

struct resource_loader *
resource_loader_new(pool_t pool, struct hstock *tcp_stock,
                    struct hstock *fcgi_stock, struct hstock *was_stock,
                    struct hstock *delegate_stock)
{
    assert(tcp_stock != NULL);
    assert(fcgi_stock != NULL);

    struct resource_loader *rl = p_malloc(pool, sizeof(*rl));

    rl->tcp_stock = tcp_stock;
    rl->fcgi_stock = fcgi_stock;
    rl->was_stock = was_stock;
    rl->delegate_stock = delegate_stock;

    return rl;
}

static const char *
extract_remote_addr(const struct strmap *headers)
{
    const char *xff = strmap_get_checked(headers, "x-forwarded-for");
    if (xff == NULL)
        return "";

    /* extract the last host name in X-Forwarded-For */
    const char *p = strrchr(xff, ',');
    if (p == NULL)
        p = xff;

    while (*p == ' ')
        ++p;

    return p;
}

static const char *
extract_remote_host(pool_t pool, const struct strmap *headers)
{
    const char *remote_addr = extract_remote_addr(headers);
    const char *colon = strchr(remote_addr, ':');
    if (colon != NULL && strchr(colon + 1, ':') == NULL)
        return p_strndup(pool, remote_addr, colon - remote_addr);

    /* XXX handle IPv6 addresses properly */
    return remote_addr;
}

static const char *
extract_server_name(pool_t pool, const struct strmap *headers,
                    unsigned *port_r)
{
    const char *p = strmap_get_checked(headers, "host");
    if (p == NULL)
        return ""; /* XXX */

    const char *colon = strchr(p, ':');
    if (colon == NULL)
        return p;

    if (strchr(colon + 1, ':') != NULL)
        /* XXX handle IPv6 addresses properly */
        return p;

    char *endptr;
    unsigned port = strtoul(colon + 1, &endptr, 10);
    if (endptr > colon + 1 && *endptr == 0)
        *port_r = port;

    return p_strndup(pool, p, colon - p);
}

void
resource_loader_request(struct resource_loader *rl, pool_t pool,
                        http_method_t method,
                        const struct resource_address *address,
                        http_status_t status, struct strmap *headers,
                        istream_t body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    assert(rl != NULL);
    assert(pool != NULL);
    assert(address != NULL);

    switch (address->type) {
        const char *server_name;
        unsigned server_port;

    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        if (body != NULL)
            /* static files cannot receive a request body, close it */
            istream_close(body);

        if (address->u.local.delegate != NULL) {
            if (rl->delegate_stock == NULL) {
                http_response_handler_direct_abort(handler, handler_ctx);
                return;
            }

            delegate_stock_request(rl->delegate_stock, pool,
                                   address->u.local.delegate,
                                   address->u.local.document_root,
                                   address->u.local.jail,
                                   address->u.local.path,
                                   address->u.local.content_type,
                                   handler, handler_ctx,
                                   async_ref);
            return;
        }

        static_file_get(pool, address->u.local.path,
                        address->u.local.content_type,
                        handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_PIPE:
        pipe_filter(pool, address->u.cgi.path,
                    address->u.cgi.args, address->u.cgi.num_args,
                    status, headers, body,
                    handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_CGI:
        cgi_new(pool, address->u.cgi.jail,
                address->u.cgi.interpreter, address->u.cgi.action,
                address->u.cgi.path,
                method, resource_address_cgi_uri(pool, address),
                address->u.cgi.script_name,
                address->u.cgi.path_info,
                address->u.cgi.query_string,
                address->u.cgi.document_root,
                headers, body,
                handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_FASTCGI:
        fcgi_request(pool, rl->fcgi_stock, address->u.cgi.jail,
                     address->u.cgi.action,
                     address->u.cgi.path,
                     method, resource_address_cgi_uri(pool, address),
                     address->u.cgi.script_name,
                     address->u.cgi.path_info,
                     address->u.cgi.query_string,
                     address->u.cgi.document_root,
                     headers, body,
                     address->u.cgi.args, address->u.cgi.num_args,
                     handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_WAS:
        was_request(pool, rl->was_stock, address->u.cgi.jail,
                    address->u.cgi.action,
                    address->u.cgi.path,
                    method, resource_address_cgi_uri(pool, address),
                    address->u.cgi.script_name,
                    address->u.cgi.path_info,
                    address->u.cgi.query_string,
                    address->u.cgi.document_root,
                    headers, body,
                    address->u.cgi.args, address->u.cgi.num_args,
                    handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_HTTP:
        http_request(pool, rl->tcp_stock,
                     method, address->u.http,
                     headers_dup(pool, headers), body,
                     handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_AJP:
        server_port = 80;
        server_name = extract_server_name(pool, headers, &server_port);
        ajp_stock_request(pool, rl->tcp_stock,
                          "http", extract_remote_addr(headers),
                          extract_remote_host(pool, headers),
                          server_name, server_port,
                          false,
                          method, address->u.http,
                          headers, body,
                          handler, handler_ctx, async_ref);
        return;
    }

    /* the resource could not be located, abort the request */

    if (body != NULL)
        istream_close(body);

    http_response_handler_direct_abort(handler, handler_ctx);
}
