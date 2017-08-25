/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_HTTP_RESPONSE_HXX
#define BENG_PROXY_HTTP_RESPONSE_HXX

#include "http/Status.h"

#include <utility>
#include <exception>

#include <assert.h>

struct pool;
class StringMap;
class Istream;

/**
 * Definition of the HTTP response handler.
 */
class HttpResponseHandler {
protected:
    virtual void OnHttpResponse(http_status_t status, StringMap &&headers,
                                Istream *body) = 0;

    virtual void OnHttpError(std::exception_ptr ep) = 0;

public:
    void InvokeResponse(http_status_t status, StringMap &&headers,
                        Istream *body) {
        assert(http_status_is_valid(status));
        assert(!http_status_is_empty(status) || body == nullptr);

        OnHttpResponse(status, std::move(headers), body);
    }

    /**
     * Sends a plain-text message.
     */
    void InvokeResponse(struct pool &pool,
                        http_status_t status, const char *msg);

    void InvokeError(std::exception_ptr ep) {
        assert(ep);

        OnHttpError(ep);
    }
};

struct http_response_handler_ref {
    HttpResponseHandler *handler;

#ifndef NDEBUG
    bool used = false;

    bool IsUsed() const {
        return used;
    }
#endif

    http_response_handler_ref() = default;

    explicit constexpr http_response_handler_ref(HttpResponseHandler &_handler)
        :handler(&_handler) {}

    bool IsDefined() const {
        return handler != nullptr;
    }

    void Clear() {
        handler = nullptr;
    }

    void Set(HttpResponseHandler &_handler) {
        handler = &_handler;

#ifndef NDEBUG
        used = false;
#endif
    }

    void InvokeResponse(http_status_t status, StringMap &&headers,
                        Istream *body) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeResponse(status, std::move(headers), body);
    }

    /**
     * Sends a plain-text message.
     */
    void InvokeMessage(struct pool &pool,
                       http_status_t status, const char *msg) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeResponse(pool, status, msg);
    }

    void InvokeError(std::exception_ptr ep) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeError(ep);
    }
};

#endif
