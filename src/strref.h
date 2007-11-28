/*
 * String reference struct.  Useful for taking cheap substrings of an
 * existing string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRREF_H
#define __BENG_STRREF_H

#include "compiler.h"
#include "pool.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

struct strref {
    size_t length;
    const char *data;
};

static attr_always_inline const char *
strref_end(const struct strref *s)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);

    return s->data + s->length;
}

static attr_always_inline void
strref_clear(struct strref *s)
{
    assert(s != NULL);

    s->length = 0;
#ifndef NDEBUG
    s->data = (const char*)0x02020202;
#endif
}

static attr_always_inline void
strref_null(struct strref *s)
{
    assert(s != NULL);

    s->length = 0;
    s->data = NULL;
}

static attr_always_inline void
strref_set(struct strref *s, const char *p, size_t length)
{
    assert(s != NULL);
    assert(p != NULL);

    s->length = length;
    s->data = p;
}

static attr_always_inline void
strref_set_c(struct strref *s, const char *p)
{
    assert(s != NULL);
    assert(p != NULL);

    s->length = strlen(p);
    s->data = p;
}

static attr_always_inline void
strref_set2(struct strref *s, const char *start, const char *end)
{
    assert(s != NULL);
    assert(start != NULL);
    assert(end != NULL);
    assert(start <= end);

    s->length = end - start;
    s->data = start;
}

static attr_always_inline void
strref_right(struct strref *dest, const struct strref *src, const char *start)
{
    assert(dest != NULL);
    assert(src != NULL);
    assert(src->data != NULL || src->length == 0);
    assert(start >= src->data && start <= strref_end(src));

    strref_set2(dest, start, strref_end(src));
}

static inline int
strref_is_null(const struct strref *s)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);

    return s->data == NULL;
}

static attr_always_inline int
strref_is_empty(const struct strref *s)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);

    return s->length == 0;
}

static attr_always_inline char
strref_last(const struct strref *s)
{
    assert(s != NULL);
    assert(s->length > 0);
    assert(s->data != NULL);

    return s->data[s->length - 1];
}

static attr_always_inline char *
strref_dup(pool_t pool, const struct strref *s)
{
    assert(pool != NULL);
    assert(s != NULL);

    return p_strndup(pool, s->data, s->length);
}

static attr_always_inline int
strref_cmp(const struct strref *s,
           const char *p, size_t length)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);
    assert(p != NULL || length == 0);

    if (s->length != length)
        return 1; /* XXX -1 or 1? */

    return memcmp(s->data, p, length);
}

static attr_always_inline int
strref_cmp2(const struct strref *a, const struct strref *b)
{
    assert(a != NULL);
    assert(b != NULL);

    return strref_cmp(a, b->data, b->length);
}

static attr_always_inline int
strref_ends_with_n(const struct strref *s,
                   const char *p, size_t length)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);

    return s->length >= length &&
        memcmp(s->data + s->length - length, p, length) == 0;
}

static attr_always_inline const char *
strref_chr(const struct strref *s, char ch)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);

    return memchr(s->data, ch, s->length);
}

#endif
