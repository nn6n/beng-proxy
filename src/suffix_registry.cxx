/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "suffix_registry.hxx"
#include "tcache.hxx"
#include "TranslateHandler.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "widget_view.hxx"
#include "pool.hxx"

struct SuffixRegistryLookup {
    TranslateRequest request;

    const SuffixRegistryHandler &handler;
    void *const handler_ctx;

    SuffixRegistryLookup(ConstBuffer<void> payload,
                         const char *suffix,
                         const SuffixRegistryHandler &_handler, void *_ctx)
        :handler(_handler), handler_ctx(_ctx) {
        request.Clear();
        request.content_type_lookup = payload;
        request.suffix = suffix;
    }
};

/*
 * TranslateHandler
 *
 */

static void
suffix_translate_response(TranslateResponse &response, void *ctx)
{
    SuffixRegistryLookup &lookup = *(SuffixRegistryLookup *)ctx;

    lookup.handler.success(response.content_type,
                           response.views != nullptr
                           ? response.views->transformation
                           : nullptr,
                           lookup.handler_ctx);
}

static void
suffix_translate_error(GError *error, void *ctx)
{
    SuffixRegistryLookup &lookup = *(SuffixRegistryLookup *)ctx;

    lookup.handler.error(error, lookup.handler_ctx);
}

static const TranslateHandler suffix_translate_handler = {
    .response = suffix_translate_response,
    .error = suffix_translate_error,
};

/*
 * constructor
 *
 */

void
suffix_registry_lookup(struct pool &pool,
                       struct tcache &tcache,
                       ConstBuffer<void> payload,
                       const char *suffix,
                       const SuffixRegistryHandler &handler, void *ctx,
                       struct async_operation_ref &async_ref)
{
    auto lookup = NewFromPool<SuffixRegistryLookup>(pool,
                                                    payload, suffix,
                                                    handler, ctx);

    translate_cache(pool, tcache, lookup->request,
                    suffix_translate_handler, lookup, async_ref);
}
