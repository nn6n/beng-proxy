/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "namespace_options.hxx"
#include "mount_list.hxx"
#include "pool.hxx"
#include "pivot_root.h"
#include "bind_mount.h"

#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/prctl.h>

#if !defined(MS_PRIVATE) && !defined(MS_REC)
/* necessary on Debian Squeeze */
#include <linux/fs.h>
#endif

#ifndef __linux
#error This library requires Linux
#endif

static int namespace_uid, namespace_gid;

void
namespace_options_global_init(void)
{
    /* at this point, we have to remember the original uid/gid to be
       able to set up the uid/gid mapping for user namespaces; after
       the clone(), it's too late, we'd only see 65534 */
    namespace_uid = geteuid();
    namespace_gid = getegid();

    /* restore the "dumpable" flag which was cleared by setreuid()
       during daemon setup; that is necessary for the proc filesystem
       to allow access to uid_map/gid_map; read the task_dumpable()
       checks in linux/fs/proc/base.c for more information */
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
}

void
namespace_options_init(struct namespace_options *options)
{
    options->enable_user = false;
    options->enable_pid = false;
    options->enable_network = false;
    options->enable_mount = false;
    options->mount_proc = false;
    options->mount_tmp_tmpfs = false;
    options->pivot_root = nullptr;
    options->home = nullptr;
    options->mount_home = nullptr;
    options->mounts = nullptr;
    options->hostname = nullptr;
}

namespace_options::namespace_options(struct pool *pool,
                                     const namespace_options &src)
        :enable_user(src.enable_user),
         enable_pid(src.enable_pid),
         enable_network(src.enable_network),
         enable_mount(src.enable_mount),
         mount_proc(src.mount_proc),
         mount_tmp_tmpfs(src.mount_tmp_tmpfs),
         pivot_root(p_strdup_checked(pool, src.pivot_root)),
         home(p_strdup_checked(pool, src.home)),
         mount_home(p_strdup_checked(pool, src.mount_home)),
         mounts(mount_list_dup(pool, src.mounts)),
         hostname(p_strdup_checked(pool, src.hostname))
{
}

void
namespace_options_copy(struct pool *pool, struct namespace_options *dest,
                       const struct namespace_options *src)
{
    *dest = *src;

    dest->pivot_root = p_strdup_checked(pool, src->pivot_root);
    dest->home = p_strdup_checked(pool, src->home);
    dest->mount_home = p_strdup_checked(pool, src->mount_home);
    dest->mounts = mount_list_dup(pool, src->mounts);
    dest->hostname = p_strdup_checked(pool, src->hostname);
}

gcc_pure
int
namespace_options_clone_flags(const struct namespace_options *options,
                              int flags)
{
    if (options->enable_user)
        flags |= CLONE_NEWUSER;
    if (options->enable_pid)
        flags |= CLONE_NEWPID;
    if (options->enable_network)
        flags |= CLONE_NEWNET;
    if (options->enable_mount)
        flags |= CLONE_NEWNS;
    if (options->hostname != nullptr)
        flags |= CLONE_NEWUTS;

    return flags;
}

void
namespace_options_unshare(const struct namespace_options *options)
{
    int unshare_flags = namespace_options_clone_flags(options, 0);

    if (unshare_flags != 0 && unshare(unshare_flags) < 0) {
        fprintf(stderr, "unshare(0x%x) failed: %s\n",
                unshare_flags, strerror(errno));
        _exit(2);
    }
}

static void
write_file(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY|O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }

    if (write(fd, data, strlen(data)) < 0) {
        fprintf(stderr, "write('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }

    close(fd);
}

static void
setup_uid_map(void)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", namespace_uid, namespace_uid);
    write_file("/proc/self/uid_map", buffer);
}

static void
setup_gid_map(void)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", namespace_gid, namespace_gid);
    write_file("/proc/self/gid_map", buffer);
}

void
namespace_options_setup(const struct namespace_options *options)
{
    /* set up UID/GID mapping in the old /proc */
    if (options->enable_user) {
        setup_gid_map();
        setup_uid_map();
    }

    if (options->enable_mount)
        /* convert all "shared" mounts to "private" mounts */
        mount(nullptr, "/", nullptr, MS_PRIVATE|MS_REC, nullptr);

    const char *const new_root = options->pivot_root;
    const char *const put_old = "mnt";

    if (new_root != nullptr) {
        /* first bind-mount the new root onto itself to "unlock" the
           kernel's mount object (flag MNT_LOCKED) in our namespace;
           without this, the kernel would not allow an unprivileged
           process to pivot_root to it */
        bind_mount(new_root, new_root, MS_NOSUID|MS_RDONLY);

        /* release a reference to the old root */
        if (chdir(new_root) < 0) {
            fprintf(stderr, "chdir('%s') failed: %s\n",
                    new_root, strerror(errno));
            _exit(2);
        }

        /* enter the new root */
        int result = my_pivot_root(new_root, put_old);
        if (result < 0) {
            fprintf(stderr, "pivot_root('%s') failed: %s\n",
                    new_root, strerror(-result));
            _exit(2);
        }
    }

    if (options->mount_proc &&
        mount("none", "/proc", "proc", MS_NOEXEC|MS_NOSUID|MS_NODEV|MS_RDONLY, nullptr) < 0) {
        fprintf(stderr, "mount('/proc') failed: %s\n",
                strerror(errno));
        _exit(2);
    }

    if (options->mount_home != nullptr || options->mounts != nullptr) {
        /* go to /mnt so we can refer to the old directories with a
           relative path */

        const char *path = new_root != nullptr ? "/mnt" : "/";

        if (chdir(path) < 0) {
            fprintf(stderr, "chdir('%s') failed: %s\n", path, strerror(errno));
            _exit(2);
        }
    }

    if (options->mount_home != nullptr) {
        assert(options->home != nullptr);
        assert(*options->home == '/');

        bind_mount(options->home + 1, options->mount_home, MS_NOSUID|MS_NODEV);
    }

    mount_list_apply(options->mounts);

    if (new_root != nullptr && (options->mount_home != nullptr ||
                             options->mounts != nullptr)) {
        /* back to the new root */
        if (chdir("/") < 0) {
            fprintf(stderr, "chdir('/') failed: %s\n", strerror(errno));
            _exit(2);
        }
    }

    if (new_root != nullptr) {
        /* get rid of the old root */
        if (umount2(put_old, MNT_DETACH) < 0) {
            fprintf(stderr, "umount('%s') failed: %s",
                    put_old, strerror(errno));
            _exit(2);
        }
    }

    if (options->mount_tmp_tmpfs &&
        mount("none", "/tmp", "tmpfs", MS_NODEV|MS_NOEXEC|MS_NOSUID,
              "size=16M,nr_inodes=256,mode=1777") < 0) {
        fprintf(stderr, "mount('/tmp') failed: %s\n",
                strerror(errno));
        _exit(2);
    }

    if (options->hostname != nullptr &&
        sethostname(options->hostname, strlen(options->hostname)) < 0) {
        fprintf(stderr, "sethostname() failed: %s", strerror(errno));
        _exit(2);
    }
}

char *
namespace_options_id(const struct namespace_options *options, char *p)
{
    if (options->enable_user)
        p = (char *)mempcpy(p, ";uns", 4);

    if (options->enable_pid)
        p = (char *)mempcpy(p, ";pns", 4);

    if (options->enable_network)
        p = (char *)mempcpy(p, ";nns", 4);

    if (options->enable_mount) {
        p = (char *)(char *)mempcpy(p, ";mns", 4);

        if (options->pivot_root != nullptr) {
            p = (char *)mempcpy(p, ";pvr=", 5);
            p = stpcpy(p, options->pivot_root);
        }

        if (options->mount_proc)
            p = (char *)mempcpy(p, ";proc", 5);

        if (options->mount_proc)
            p = (char *)mempcpy(p, ";tmpfs", 6);

        if (options->mount_home != nullptr) {
            p = (char *)mempcpy(p, ";h:", 3);
            p = stpcpy(p, options->home);
            *p++ = '=';
            p = stpcpy(p, options->mount_home);
        }
    }

    if (options->hostname != nullptr) {
        p = (char *)mempcpy(p, ";uts=", 5);
        p = stpcpy(p, options->hostname);
    }

    return p;
}
