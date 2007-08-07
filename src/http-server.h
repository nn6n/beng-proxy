/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_H
#define __BENG_HTTP_SERVER_H

/*#include <sys/socket.h>*/
#include <sys/types.h>
#include <event.h>

typedef enum {
    HTTP_METHOD_NULL = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_INVALID,
} http_method_t;

typedef enum {
    HTTP_STATUS_OK = 200,
} http_status_t;

typedef struct http_server_connection *http_server_connection_t;

struct http_server_request {
    http_server_connection_t connection;
    http_method_t method;
    char *uri;
};

typedef void (*http_server_callback_t)(struct http_server_request *request,
                                       /*const void *body, size_t body_length,*/
                                       void *ctx);

int
http_server_connection_new(int fd,
                           http_server_callback_t callback, void *ctx,
                           http_server_connection_t *connection_r);

void
http_server_connection_free(http_server_connection_t *connection_r);

void
http_server_send_message(http_server_connection_t connection,
                         http_status_t status, const char *msg);

#endif
