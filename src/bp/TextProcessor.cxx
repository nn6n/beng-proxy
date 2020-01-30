/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "TextProcessor.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SubstIstream.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "pool/pool.hxx"
#include "escape_html.hxx"
#include "escape_pool.hxx"

#include <assert.h>

gcc_pure
static bool
text_processor_allowed_content_type(const char *content_type)
{
	assert(content_type != NULL);

	return strncmp(content_type, "text/", 5) == 0 ||
		strncmp(content_type, "application/json", 16) == 0 ||
		strncmp(content_type, "application/javascript", 22) == 0;
}

bool
text_processor_allowed(const StringMap &headers)
{
	const char *content_type = headers.Get("content-type");
	return content_type != NULL &&
		text_processor_allowed_content_type(content_type);
}

gcc_pure
static const char *
base_uri(struct pool *pool, const char *absolute_uri)
{
	const char *p;

	if (absolute_uri == NULL)
		return NULL;

	p = strchr(absolute_uri, ';');
	if (p == NULL) {
		p = strchr(absolute_uri, '?');
		if (p == NULL)
			return absolute_uri;
	}

	return p_strndup(pool, absolute_uri, p - absolute_uri);
}

static void
SubstAddEscaped(struct pool &pool, SubstTree &subst, const char *a,
		StringView b) noexcept
{
	if (!b.empty())
		b = escape_dup(&pool, &html_escape_class, b);

	subst.Add(pool, a, b);
}

static SubstTree
processor_subst_beng_widget(struct pool &pool,
			    const Widget &widget,
			    const WidgetContext &ctx)
{
	SubstTree subst;
	SubstAddEscaped(pool, subst, "&c:type;", widget.class_name);
	SubstAddEscaped(pool, subst, "&c:class;", widget.GetQuotedClassName());
	SubstAddEscaped(pool, subst, "&c:local;", widget.cls->local_uri);
	SubstAddEscaped(pool, subst, "&c:id;", widget.id);
	SubstAddEscaped(pool, subst, "&c:path;", widget.GetIdPath());
	SubstAddEscaped(pool, subst, "&c:prefix;", widget.GetPrefix());
	SubstAddEscaped(pool, subst, "&c:uri;", ctx.absolute_uri);
	SubstAddEscaped(pool, subst, "&c:base;", base_uri(&pool, ctx.uri));
	SubstAddEscaped(pool, subst, "&c:frame;", strmap_get_checked(ctx.args, "frame"));
	SubstAddEscaped(pool, subst, "&c:view;", widget.GetEffectiveView()->name);
	SubstAddEscaped(pool, subst, "&c:session;", nullptr); /* obsolete as of version 15.29 */
	return subst;
}

UnusedIstreamPtr
text_processor(struct pool &pool, UnusedIstreamPtr input,
	       const Widget &widget, const WidgetContext &ctx)
{
	return istream_subst_new(&pool, std::move(input),
				 processor_subst_beng_widget(pool, widget, ctx));
}
