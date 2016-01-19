/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "NameCache.hxx"
#include "certdb/Config.hxx"
#include "event/Callback.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

CertNameCache::CertNameCache(const CertDatabaseConfig &config,
                             CertNameCacheHandler &_handler)
    :handler(_handler),
     conn(config.connect.c_str(), config.schema.c_str(), *this),
     update_timer(MakeSimpleEventCallback(CertNameCache, OnUpdateTimer), this)
{
}

bool
CertNameCache::Lookup(const char *_host) const
{
    if (!complete)
        /* we can't give reliable results until the cache is
           complete */
        return true;

    const std::string host(_host);

    const std::unique_lock<std::mutex> lock(mutex);
    return names.find(host) != names.end() ||
        alt_names.find(host) != alt_names.end();
}

void
CertNameCache::OnUpdateTimer()
{
    assert(conn.IsReady());

    daemon_log(4, "updating certificate database name cache\n");

    n_added = n_updated = n_deleted = 0;

    if (complete)
        conn.SendQuery(*this,
                       "SELECT common_name, alt_names, "
                       "modified, deleted "
                       " FROM server_certificate"
                       " WHERE modified>$1"
                       " ORDER BY modified",
                       latest.c_str());
    else
        /* omit deleted certificates during the initial download
           (until our mirror is complete) */
        conn.SendQuery(*this,
                       "SELECT common_name, alt_names, "
                       "modified "
                       " FROM server_certificate"
                       " WHERE NOT deleted"
                       " ORDER BY modified");

    conn.SetSingleRowMode();
}

void
CertNameCache::ScheduleUpdate()
{
    static constexpr struct timeval update_delay = { 0, 200000 };

    if (!update_timer.IsPending())
        update_timer.Add(update_delay);
}

inline void
CertNameCache::AddAltNames(const std::string &common_name,
                           std::list<std::string> &&list)
{
    for (auto &&a : list) {
        /* create the alt_name if it doesn't exist yet */
        auto i = alt_names.emplace(std::move(a), std::set<std::string>());
        /* add the common_name to the set */
        i.first->second.emplace(common_name);
    }
}

inline void
CertNameCache::RemoveAltNames(const std::string &common_name,
                              std::list<std::string> &&list)
{
    for (auto &&a : list) {
        auto i = alt_names.find(std::move(a));
        if (i != alt_names.end()) {
            /* this alt_name exists */
            auto j = i->second.find(common_name);
            if (j != i->second.end()) {
                /* and inside, the given common_name exist; remove
                   it */
                i->second.erase(j);
                if (i->second.empty())
                    /* list is empty, no more certificates cover this
                       alt_name: remove it completely */
                    alt_names.erase(i);
            }
        }
    }
}

void
CertNameCache::OnConnect()
{
    daemon_log(5, "connected to certificate database\n");

    // TODO: make asynchronous
    conn.Execute("LISTEN modified");
    conn.Execute("LISTEN deleted");

    ScheduleUpdate();
}

void
CertNameCache::OnDisconnect()
{
    daemon_log(4, "disconnected from certificate database\n");

    UnscheduleUpdate();
}

void
CertNameCache::OnNotify(const char *name)
{
    daemon_log(5, "received notify '%s'\n", name);

    ScheduleUpdate();
}

void
CertNameCache::OnError(const char *prefix, const char *error)
{
    daemon_log(2, "%s: %s\n", prefix, error);
}

void
CertNameCache::OnResult(PgResult &&result)
{
    if (result.IsError()) {
        daemon_log(1, "query error from certificate database: %s\n",
                   result.GetErrorMessage());
        ScheduleUpdate();
        return;
    }

    const char *modified = nullptr;

    for (const auto &row : result) {
        std::string name(row.GetValue(0));
        std::list<std::string> _alt_names = row.IsValueNull(1)
            ? std::list<std::string>()
            : pg_decode_array(row.GetValue(1));
        modified = row.GetValue(2);
        const bool deleted = complete && *row.GetValue(3) == 't';

        handler.OnCertModified(name, deleted);
        for (const auto &a : _alt_names)
            handler.OnCertModified(a, deleted);

        const std::unique_lock<std::mutex> lock(mutex);

        if (deleted) {
            RemoveAltNames(name, std::move(_alt_names));

            auto i = names.find(std::move(name));
            if (i != names.end()) {
                names.erase(i);
                ++n_deleted;
            }
        } else {
            AddAltNames(name, std::move(_alt_names));

            auto i = names.emplace(std::move(name));
            if (i.second)
                ++n_added;
            else
                ++n_updated;
        }
    }

    if (modified != nullptr)
        latest = modified;
}

void
CertNameCache::OnResultEnd()
{
    daemon_log(4, "certificate database name cache: %u added, %u updated, %u deleted\n",
               n_added, n_updated, n_deleted);

    if (!complete) {
        daemon_log(4, "certificate database name cache is complete\n");
        complete = true;
    }
}

void
CertNameCache::OnResultError()
{
    ScheduleUpdate();
}
