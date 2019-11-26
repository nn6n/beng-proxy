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

/*
 * Write HTTP headers into a buffer.
 */

#ifndef BENG_PROXY_HEADER_WRITER_HXX
#define BENG_PROXY_HEADER_WRITER_HXX

class StringMap;
class GrowingBuffer;

/**
 * Begin writing a header line.  After this, you may write the value.
 * Call header_write_finish() when you're done.
 */
void
header_write_begin(GrowingBuffer &buffer, const char *name);

/**
 * Finish the current header line.
 *
 * @see header_write_begin().
 */
void
header_write_finish(GrowingBuffer &buffer);

void
header_write(GrowingBuffer &buffer, const char *key, const char *value);

void
headers_copy_one(const StringMap &in, GrowingBuffer &out,
                 const char *key);

void
headers_copy(const StringMap &in, GrowingBuffer &out,
             const char *const* keys);

void
headers_copy_all(const StringMap &in, GrowingBuffer &out);

/**
 * Like headers_copy_all(), but doesn't copy hop-by-hop headers.
 */
void
headers_copy_most(const StringMap &in, GrowingBuffer &out);

GrowingBuffer
headers_dup(const StringMap &in);

#endif