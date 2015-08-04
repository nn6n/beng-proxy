/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_options.hxx"
#include "pool.hxx"
#include "regex.hxx"
#include "util/djbhash.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

ChildOptions::ChildOptions(struct pool *pool,
                           const ChildOptions &src)
    :stderr_path(p_strdup_checked(pool, src.stderr_path)),
     expand_stderr_path(p_strdup_checked(pool, src.expand_stderr_path)),
     rlimits(src.rlimits),
     ns(pool, src.ns),
     jail(pool, src.jail)
{
}

void
ChildOptions::CopyFrom(struct pool *pool, const ChildOptions *src)
{
    stderr_path = p_strdup_checked(pool, src->stderr_path);
    expand_stderr_path = p_strdup_checked(pool, src->expand_stderr_path);

    rlimits = src->rlimits;
    ns.CopyFrom(*pool, src->ns);
    jail.CopyFrom(*pool, src->jail);
}

bool
ChildOptions::Expand(struct pool &pool, const GMatchInfo *match_info,
                     GError **error_r)
{
    if (expand_stderr_path != nullptr) {
        stderr_path = expand_string_unescaped(&pool, expand_stderr_path,
                                              match_info, error_r);
        if (stderr_path == nullptr)
            return false;
    }

    return ns.Expand(pool, match_info, error_r) &&
        jail.Expand(pool, match_info, error_r);
}

char *
ChildOptions::MakeId(char *p) const
{
    if (stderr_path != nullptr)
        p += sprintf(p, ";e%08x", djb_hash_string(stderr_path));

    p = rlimits.MakeId(p);
    p = ns.MakeId(p);
    p = jail.MakeId(p);
    return p;
}

int
ChildOptions::OpenStderrPath() const
{
    assert(stderr_path != nullptr);

    return open(stderr_path, O_CREAT|O_WRONLY|O_APPEND|O_CLOEXEC|O_NOCTTY,
                0666);
}

void
ChildOptions::SetupStderr(bool stdout) const
{
    if (stderr_path == nullptr)
        return;

    int fd = OpenStderrPath();
    if (fd < 0) {
        fprintf(stderr, "open('%s') failed: %s\n",
                stderr_path, strerror(errno));
        _exit(2);
    }

    if (fd != 2)
        dup2(fd, 2);

    if (stdout && fd != 1)
        dup2(fd, 1);

    close(fd);
}
