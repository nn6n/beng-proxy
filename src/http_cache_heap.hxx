/*
 * Caching HTTP responses in heap memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_HEAP_HXX
#define BENG_PROXY_HTTP_CACHE_HEAP_HXX

#include <http/status.h>

#include <stddef.h>

struct http_cache_heap {
    struct pool *pool;

    struct cache *cache;

    struct slice_pool *slice_pool;
};

struct pool;
class Rubber;
struct strmap;
struct cache_stats;
struct http_cache_info;

static inline void
http_cache_heap_clear(struct http_cache_heap *cache)
{
    cache->cache = nullptr;
}

static inline bool
http_cache_heap_is_defined(const struct http_cache_heap *cache)
{
    return cache->cache != nullptr;
}

void
http_cache_heap_init(struct http_cache_heap *cache,
                     struct pool &pool, size_t max_size);

void
http_cache_heap_deinit(struct http_cache_heap *cache);

void
http_cache_heap_get_stats(const struct http_cache_heap *cache,
                          const Rubber *rubber,
                          struct cache_stats *data);

struct http_cache_document *
http_cache_heap_get(struct http_cache_heap *cache, const char *uri,
                    struct strmap *request_headers);

void
http_cache_heap_put(struct http_cache_heap *cache,
                    const char *url,
                    const struct http_cache_info *info,
                    struct strmap *request_headers,
                    http_status_t status,
                    const struct strmap *response_headers,
                    Rubber *rubber, unsigned rubber_id, size_t size);

void
http_cache_heap_remove(struct http_cache_heap *cache, const char *url,
                       struct http_cache_document *document);

void
http_cache_heap_remove_url(struct http_cache_heap *cache, const char *url,
                           struct strmap *headers);

void
http_cache_heap_flush(struct http_cache_heap *cache);

void
http_cache_heap_lock(struct http_cache_document *document);

void
http_cache_heap_unlock(struct http_cache_heap *cache,
                       struct http_cache_document *document);

struct istream *
http_cache_heap_istream(struct pool *pool, struct http_cache_heap *cache,
                        struct http_cache_document *document);

#endif
