/*
 * Serve HTTP requests from another HTTP/AJP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "request-forward.h"
#include "http-server.h"
#include "http-cache.h"
#include "http-response.h"
#include "uri-address.h"
#include "global.h"
#include "cookie_client.h"
#include "uri-extract.h"
#include "strref-pool.h"
#include "strmap.h"
#include "http-response.h"
#include "istream-impl.h"

static void
proxy_collect_cookies(struct request *request2, const struct strmap *headers)
{
    const struct translate_response *tr = request2->translate.response;
    const char *key = "set-cookie2";
    const char *cookies;
    struct session *session;

    if (headers == NULL)
        return;

    cookies = strmap_get(headers, key);
    if (cookies == NULL) {
        key = "set-cookie";
        cookies = strmap_get(headers, key);
        if (cookies == NULL)
            return;
    }

    const char *host_and_port = request2->translate.response->cookie_host;
    if (host_and_port == NULL)
        host_and_port = resource_address_host_and_port(&tr->address,
                                                       request2->request->pool);
    if (host_and_port == NULL)
        return;

    const char *path = resource_address_uri_path(&tr->address);
    if (path == NULL)
        return;

    session = request_make_session(request2);
    if (session == NULL)
        return;

    do {
        cookie_jar_set_cookie2(session->cookies, cookies, host_and_port, path);

        cookies = strmap_get_next(headers, key, cookies);
    } while (cookies != NULL);

    session_put(session);
}

static void
proxy_response(http_status_t status, struct strmap *headers,
               struct istream *body, void *ctx)
{
    struct request *request2 = ctx;

#ifndef NDEBUG
    const struct translate_response *tr = request2->translate.response;
    assert(tr->address.type == RESOURCE_ADDRESS_HTTP ||
           tr->address.type == RESOURCE_ADDRESS_AJP ||
           resource_address_is_cgi_alike(&tr->address));
#endif

    proxy_collect_cookies(request2, headers);

    http_response_handler_direct_response(&response_handler, request2,
                                          status, headers, body);
}

static void
proxy_abort(GError *error, void *ctx)
{
    struct request *request2 = ctx;

    http_response_handler_direct_abort(&response_handler, request2, error);
}

static const struct http_response_handler proxy_response_handler = {
    .response = proxy_response,
    .abort = proxy_abort,
};

void
proxy_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    struct forward_request forward;

    assert(tr->address.type == RESOURCE_ADDRESS_HTTP ||
           tr->address.type == RESOURCE_ADDRESS_AJP ||
           resource_address_is_cgi_alike(&tr->address));

    const char *host_and_port = NULL, *uri_p = NULL;
    if (tr->address.type == RESOURCE_ADDRESS_HTTP ||
        tr->address.type == RESOURCE_ADDRESS_AJP) {
        host_and_port = tr->address.u.http->host_and_port;
        uri_p = tr->address.u.http->path;
    }

    request_forward(&forward, request2,
                    &tr->request_header_forward,
                    host_and_port, uri_p,
                    tr->address.type == RESOURCE_ADDRESS_HTTP);

    const struct resource_address *address = &tr->address;
    if (request2->translate.response->transparent &&
        (!strref_is_empty(&request2->uri.args) ||
         !strref_is_empty(&request2->uri.path_info)))
        address = resource_address_insert_args(request->pool, address,
                                               request2->uri.args.data,
                                               request2->uri.args.length,
                                               request2->uri.path_info.data,
                                               request2->uri.path_info.length);

    if (!request2->processor_focus)
        /* forward query string */
        address = resource_address_insert_query_string_from(request->pool,
                                                            address,
                                                            request->uri);

    if (resource_address_is_cgi_alike(address) &&
        address->u.cgi.uri == NULL) {
        struct resource_address *copy = resource_address_dup(request->pool,
                                                             address);

        /* pass the "real" request URI to the CGI (but without the
           "args", unless the request is "transparent") */
        if (request2->translate.response->transparent ||
            strref_is_empty(&request2->uri.args))
            copy->u.cgi.uri = request->uri;
        else if (strref_is_empty(&request2->uri.query))
            copy->u.cgi.uri = strref_dup(request->pool, &request2->uri.base);
        else
            copy->u.cgi.uri = p_strncat(request->pool,
                                        request2->uri.base.data,
                                        request2->uri.base.length,
                                        "?", (size_t)1,
                                        request2->uri.query.data,
                                        request2->uri.query.length,
                                        NULL);

        address = copy;
    }

#ifdef SPLICE
    if (forward.body != NULL)
        forward.body = istream_pipe_new(request->pool, forward.body,
                                        global_pipe_stock);
#endif

    http_cache_request(global_http_cache, request->pool,
                       session_id_low(request2->session_id),
                       forward.method, address,
                       forward.headers, forward.body,
                       &proxy_response_handler, request2,
                       &request2->async_ref);
}
