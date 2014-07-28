/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_document.hxx"
#include "http_cache_rfc.hxx"
#include "strmap.hxx"

http_cache_document::http_cache_document(struct pool &pool,
                                         const struct http_cache_response_info &_info,
                                         const struct strmap *request_headers,
                                         http_status_t _status,
                                         const struct strmap *_response_headers)
    :info(pool, _info),
     vary(_info.vary != nullptr
          ? http_cache_copy_vary(pool, _info.vary, request_headers)
          : nullptr),
     status(_status),
     response_headers(_response_headers != nullptr
                      ? strmap_dup(&pool, _response_headers)
                      : nullptr)
{
    assert(http_status_is_valid(_status));
}

bool
http_cache_document::VaryFits(const struct strmap *request_headers) const
{
    return http_cache_vary_fits(vary, request_headers);
}
