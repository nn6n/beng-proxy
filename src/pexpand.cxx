/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pexpand.hxx"
#include "expand.hxx"
#include "regex.hxx"
#include "AllocatorPtr.hxx"
#include "uri/uri_escape.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

const char *
expand_string(AllocatorPtr alloc, const char *src,
              const MatchInfo &match_info)
{
    assert(src != nullptr);
    assert(match_info.IsDefined());

    const size_t length = ExpandStringLength(src, match_info);
    const auto buffer = alloc.NewArray<char>(length + 1);

    struct Result {
        char *q;

        explicit Result(char *_q):q(_q) {}

        void Append(char ch) {
            *q++ = ch;
        }

        void Append(const char *p) {
            q = stpcpy(q, p);
        }

        void Append(const char *p, size_t _length) {
            q = (char *)mempcpy(q, p, _length);
        }

        void AppendValue(const char *p, size_t _length) {
            Append(p, _length);
        }
    };

    Result result(buffer);
    ExpandString(result, src, match_info);

    assert(result.q == buffer + length);
    *result.q = 0;

    return buffer;
}

const char *
expand_string_unescaped(AllocatorPtr alloc, const char *src,
                        const MatchInfo &match_info)
{
    assert(src != nullptr);
    assert(match_info.IsDefined());

    const size_t length = ExpandStringLength(src, match_info);
    const auto buffer = alloc.NewArray<char>(length + 1);

    struct Result {
        char *q;

        explicit Result(char *_q):q(_q) {}

        void Append(char ch) {
            *q++ = ch;
        }

        void Append(const char *p) {
            q = stpcpy(q, p);
        }

        void Append(const char *p, size_t _length) {
            q = (char *)mempcpy(q, p, _length);
        }

        void AppendValue(const char *p, size_t _length) {
            q = uri_unescape(q, {p, _length});
            if (q == nullptr)
                throw std::runtime_error("Malformed URI escape");
        }
    };

    Result result(buffer);
    ExpandString(result, src, match_info);

    assert(result.q <= buffer + length);
    *result.q = 0;

    return buffer;
}
