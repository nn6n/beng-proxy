/*
 * Escaping with a static destination buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ESCAPE_STATIC_HXX
#define BENG_PROXY_ESCAPE_STATIC_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct escape_class;
struct StringView;

/**
 * Unescape the given string into a global static buffer.  Returns
 * NULL when the string is too long for the buffer.
 */
gcc_pure
const char *
unescape_static(const struct escape_class *cls, StringView p);

gcc_pure
const char *
escape_static(const struct escape_class *cls, StringView p);

#endif
