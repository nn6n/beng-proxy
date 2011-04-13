/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REWRITE_URI_H
#define __BENG_REWRITE_URI_H

#include "istream.h"
#include "session.h"

struct tcache;
struct parsed_uri;
struct strmap;
struct widget;
struct strref;

enum uri_mode {
    URI_MODE_DIRECT,
    URI_MODE_FOCUS,
    URI_MODE_PARTIAL,
    URI_MODE_PROXY,
};

/**
 * @param untrusted_host the value of the UNTRUSTED translation
 * packet, or NULL if this is a "trusted" request
 * @param stateful if true, then the current request/session state is
 * taken into account (path_info and query_string)
 */
istream_t
rewrite_widget_uri(pool_t pool, pool_t widget_pool,
                   struct tcache *translate_cache,
                   const char *absolute_uri,
                   const struct parsed_uri *external_uri,
                   const char *site_name,
                   const char *untrusted_host,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode, bool stateful);

#endif
