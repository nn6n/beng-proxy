#include "tcache.h"
#include "tstock.h"
#include "translate-client.h"
#include "translate-request.h"
#include "translate-response.h"
#include "async.h"
#include "beng-proxy/translation.h"

#include <event.h>

#include <string.h>

const struct translate_response *next_response, *expected_response;

void
tstock_translate(gcc_unused struct tstock *stock, gcc_unused struct pool *pool,
                 gcc_unused const struct translate_request *request,
                 const struct translate_handler *handler, void *ctx,
                 gcc_unused struct async_operation_ref *async_ref)
{
    if (next_response != NULL)
        handler->response(next_response, ctx);
    else
        handler->error(g_error_new(translate_quark(), 0, "Error"), ctx);
}

static void
my_translate_response(const struct translate_response *response,
                      gcc_unused void *ctx)
{
    if (response == NULL) {
        assert(expected_response == NULL);
    } else {
        assert(expected_response != NULL);
        assert(response->address.type == RESOURCE_ADDRESS_LOCAL);
        assert(expected_response->address.type == RESOURCE_ADDRESS_LOCAL);
        assert(strcmp(response->address.u.local.path,
                      expected_response->address.u.local.path) == 0);
    }
}

static void
my_translate_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    assert(expected_response == NULL);

    g_error_free(error);
}

static const struct translate_handler my_translate_handler = {
    .response = my_translate_response,
    .error = my_translate_error,
};

static void
test_basic(struct pool *pool, struct tcache *cache)
{
    static const struct translate_request request1 = {
        .uri = "/",
    };
    static const struct translate_request request2 = {
        .uri = "/foo/bar.html",
    };
    static const struct translate_request request3 = {
        .uri = "/foo/index.html",
    };
    static const struct translate_request request4 = {
        .uri = "/foo/",
    };
    static const struct translate_request request5 = {
        .uri = "/foo",
    };

    static const struct translate_response response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/index.html",
                },
            },
        },
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_response response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/srv/foo/bar.html",
                },
            },
        },
        .base = "/foo/",
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_response response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/srv/foo/index.html",
                },
            },
        },
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_response response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/srv/foo/",
                },
            },
        },
        .max_age = -1,
        .user_max_age = -1,
    };

    struct async_operation_ref async_ref;

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, NULL, &async_ref);

    next_response = NULL;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, NULL, &async_ref);

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, NULL, &async_ref);

    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, NULL, &async_ref);

    expected_response = NULL;
    translate_cache(pool, cache, &request5,
                    &my_translate_handler, NULL, &async_ref);
}

static void
test_vary_invalidate(struct pool *pool, struct tcache *cache)
{
    static const struct translate_request request6 = {
        .uri = "/qs",
        .query_string = "abc",
    };
    static const struct translate_request request7 = {
        .uri = "/qs",
        .query_string = "xyz",
    };
    static const struct translate_request request8 = {
        .uri = "/qs/",
        .query_string = "xyz",
    };
    static const uint16_t response5_vary[] = {
        TRANSLATE_QUERY_STRING,
    };

    static const struct translate_response response5a = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/src/qs1",
                },
            },
        },
        .max_age = -1,
        .user_max_age = -1,
        .vary = response5_vary,
        .num_vary = sizeof(response5_vary) / sizeof(response5_vary[0]),
    };

    static const struct translate_response response5b = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/src/qs2",
                },
            },
        },
        .max_age = -1,
        .user_max_age = -1,
        .vary = response5_vary,
        .num_vary = sizeof(response5_vary) / sizeof(response5_vary[0]),
    };

    static const uint16_t response5_invalidate[] = {
        TRANSLATE_QUERY_STRING,
    };

    static const struct translate_response response5c = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/src/qs3",
                },
            },
        },
        .max_age = -1,
        .user_max_age = -1,
        .vary = response5_vary,
        .num_vary = sizeof(response5_vary) / sizeof(response5_vary[0]),
        .invalidate = response5_invalidate,
        .num_invalidate = sizeof(response5_invalidate) / sizeof(response5_invalidate[0]),
    };

    struct async_operation_ref async_ref;

    next_response = expected_response = &response5a;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, NULL, &async_ref);

    next_response = expected_response = &response5b;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response5a;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response5b;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, NULL, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request8,
                    &my_translate_handler, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response5a;
    translate_cache(pool, cache, &request6,
                    &my_translate_handler, NULL, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, NULL, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request8,
                    &my_translate_handler, NULL, &async_ref);

    expected_response = &response5c;
    translate_cache(pool, cache, &request7,
                    &my_translate_handler, NULL, &async_ref);
}

static void
test_regex(struct pool *pool, struct tcache *cache)
{
    static const struct translate_request request_i1 = {
        .uri = "/regex/foo",
    };
    static const struct translate_response response_i1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/regex/other/foo",
                },
            },
        },
        .base = "/regex/",
        .inverse_regex = "\\.(jpg|html)$",
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_request request_i2 = {
        .uri = "/regex/bar",
    };
    static const struct translate_response response_i2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/regex/other/bar",
                },
            },
        },
        .base = "/regex/",
        .inverse_regex = "\\.(jpg|html)$",
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_request request1 = {
        .uri = "/regex/a/foo.jpg",
    };
    static const struct translate_response response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/regex/images/a/foo.jpg",
                },
            },
        },
        .base = "/regex/",
        .regex = "\\.jpg$",
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_request request2 = {
        .uri = "/regex/b/foo.html",
    };
    static const struct translate_response response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/regex/html/b/foo.html",
                },
            },
        },
        .base = "/regex/",
        .regex = "\\.html$",
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_request request3 = {
        .uri = "/regex/c/bar.jpg",
    };
    static const struct translate_response response3 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/regex/images/c/bar.jpg",
                },
            },
        },
        .base = "/regex/",
        .regex = "\\.jpg$",
        .max_age = -1,
        .user_max_age = -1,
    };

    static const struct translate_request request4 = {
        .uri = "/regex/d/bar.html",
    };
    static const struct translate_response response4 = {
        .address = {
            .type = RESOURCE_ADDRESS_LOCAL,
            .u = {
                .local = {
                    .path = "/var/www/regex/html/d/bar.html",
                },
            },
        },
        .base = "/regex/",
        .regex = "\\.html$",
        .max_age = -1,
        .user_max_age = -1,
    };

    struct async_operation_ref async_ref;

    /* add the "inverse_regex" test to the cache first */
    next_response = expected_response = &response_i1;
    translate_cache(pool, cache, &request_i1,
                    &my_translate_handler, NULL, &async_ref);

    /* fill the cache */
    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, NULL, &async_ref);

    /* regex mismatch */
    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, NULL, &async_ref);

    /* regex match */
    next_response = NULL;
    expected_response = &response3;
    translate_cache(pool, cache, &request3,
                    &my_translate_handler, NULL, &async_ref);

    /* second regex match */
    next_response = NULL;
    expected_response = &response4;
    translate_cache(pool, cache, &request4,
                    &my_translate_handler, NULL, &async_ref);

    /* see if the "inverse_regex" cache item is still there */
    next_response = NULL;
    expected_response = &response_i2;
    translate_cache(pool, cache, &request_i2,
                    &my_translate_handler, NULL, &async_ref);
}

static void
test_expand(struct pool *pool, struct tcache *cache)
{
    struct async_operation_ref async_ref;

    /* add to cache */

    static const struct translate_request request1 = {
        .uri = "/regex-expand/b=c",
    };
    static const struct translate_response response1 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = {
                    .path = "/usr/lib/cgi-bin/foo.cgi",
                    .path_info = "/a/b=c",
                    .expand_path_info = "/a/\\1",
                },
            },
        },
        .base = "/regex-expand/",
        .regex = "^/regex-expand/(.+=.+)$",
        .max_age = -1,
        .user_max_age = -1,
    };

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1,
                    &my_translate_handler, NULL, &async_ref);

    /* check match */

    static const struct translate_request request2 = {
        .uri = "/regex-expand/d=e",
    };
    static const struct translate_response response2 = {
        .address = {
            .type = RESOURCE_ADDRESS_CGI,
            .u = {
                .cgi = {
                    .path = "/usr/lib/cgi-bin/foo.cgi",
                    .path_info = "/a/d=e",
                },
            },
        },
        .base = "/regex-expand/",
        .max_age = -1,
        .user_max_age = -1,
    };

    next_response = NULL;
    expected_response = &response2;
    translate_cache(pool, cache, &request2,
                    &my_translate_handler, NULL, &async_ref);
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    struct tstock *const translate_stock = (void *)0x1;
    struct event_base *event_base;
    struct pool *pool;
    struct tcache *cache;

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    cache = translate_cache_new(pool, translate_stock, 1024);

    /* test */

    test_basic(pool, cache);
    test_vary_invalidate(pool, cache);
    test_regex(pool, cache);
    test_expand(pool, cache);

    /* cleanup */

    translate_cache_close(cache);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
