/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "embed.h"
#include "uri.h"
#include "processor.h"
#include "widget.h"

#include <assert.h>

static istream_t
frame_top_widget(pool_t pool, struct processor_env *env,
                 struct widget *widget)
{
    http_method_t method = HTTP_METHOD_GET;
    istream_t request_body = NULL;
    struct processor_env *env2;

    assert(widget->from_request.proxy);

    if (widget->from_request.body) {
        assert(env->request_body != NULL);

        method = HTTP_METHOD_POST; /* XXX which method? */
        request_body = env->request_body;
        /* XXX what if there is no stream handler? or two? */
    }

    /* install normal embed callback on cloned env */

    env2 = processor_env_dup(pool, env);
    env2->widget_callback = embed_widget_callback;

    /* clear the response_handler in the original env, because it is
       reserved for us, and the other widgets should not use it
       anymore */
    http_response_handler_clear(&env->response_handler);

    return embed_new(pool,
                     method, widget->real_uri,
                     request_body,
                     widget,
                     env2,
                     PROCESSOR_JSCRIPT | PROCESSOR_JSCRIPT_ROOT);
}

istream_t
frame_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget)
{
    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == frame_widget_callback);
    assert(widget != NULL);

    if (widget->from_request.proxy)
        /* this widget is being proxied */
        return frame_top_widget(pool, env, widget);
    else if (widget->from_request.proxy_ref != NULL)
        /* only partial match: this is the parent of the frame
           widget */
        return embed_new(pool,
                         HTTP_METHOD_GET, widget->real_uri, NULL,
                         widget,
                         env, PROCESSOR_QUIET);
    else
        /* this widget is none of our business */
        return NULL;
}
