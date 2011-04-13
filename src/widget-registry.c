/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-registry.h"
#include "widget-class.h"
#include "processor.h"
#include "widget.h"
#include "tcache.h"
#include "translate.h"
#include "uri-address.h"
#include "transformation.h"

#include <daemon/log.h>

static void
widget_registry_lookup(pool_t pool,
                       struct tcache *tcache,
                       const char *widget_type,
                       const struct translate_handler *handler, void *ctx,
                       struct async_operation_ref *async_ref)
{
    struct translate_request *request = p_malloc(pool, sizeof(*request)); 

    request->local_address = NULL;
    request->local_address_length = 0;
    request->remote_host = NULL;
    request->host = NULL;
    request->user_agent = NULL;
    request->accept_language = NULL;
    request->authorization = NULL;
    request->uri = NULL;
    request->args = NULL;
    request->query_string = NULL;
    request->widget_type = widget_type;
    request->session = NULL;
    request->param = NULL;
    strref_null(&request->check);
    request->error_document_status = 0;

    translate_cache(pool, tcache, request,
                    handler, ctx, async_ref);
}

struct widget_class_lookup {
    pool_t pool;

    widget_class_callback_t callback;
    void *callback_ctx;
};

static void 
widget_translate_response(const struct translate_response *response, void *ctx)
{
    struct widget_class_lookup *lookup = ctx;
    struct widget_class *class;

    if (response->status != 0) {
        lookup->callback(NULL, lookup->callback_ctx);
        return;
    }

    class = p_malloc(lookup->pool, sizeof(*class));
    class->untrusted_host = response->untrusted;
    class->untrusted_prefix = response->untrusted_prefix;
    class->untrusted_site_suffix = response->untrusted_site_suffix;
    if (class->untrusted_host == NULL)
        /* compatibility with v0.7.16 */
        class->untrusted_host = response->host;
    class->stateful = response->stateful;
    if (response->views != NULL)
        class->views = *widget_view_dup_chain(lookup->pool, response->views);
    else
        widget_view_init(&class->views);

    lookup->callback(class, lookup->callback_ctx);
}

static void
widget_translate_error(GError *error, void *ctx)
{
    struct widget_class_lookup *lookup = ctx;

    daemon_log(2, "widget registry error: %s\n", error->message);
    g_error_free(error);

    lookup->callback(NULL, lookup->callback_ctx);
}

static const struct translate_handler widget_translate_handler = {
    .response = widget_translate_response,
    .error = widget_translate_error,
};

void
widget_class_lookup(pool_t pool, pool_t widget_pool,
                    struct tcache *tcache,
                    const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct widget_class_lookup *lookup = p_malloc(pool, sizeof(*lookup));

    assert(widget_type != NULL);

    lookup->pool = widget_pool;
    lookup->callback = callback;
    lookup->callback_ctx = ctx;

    widget_registry_lookup(pool, tcache, widget_type,
                           &widget_translate_handler, lookup,
                           async_ref);
}
