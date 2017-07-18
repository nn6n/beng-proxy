/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "Datagram.hxx"
#include "OneLine.hxx"
#include "http_server/Request.hxx"
#include "net/ToString.hxx"
#include "system/Error.hxx"

#include <assert.h>
#include <string.h>

AccessLogGlue::AccessLogGlue(const AccessLogConfig &_config,
                             LogClient *_client)
    :config(_config), client(_client) {}

AccessLogGlue::~AccessLogGlue()
{
    delete client;
}

static UniqueSocketDescriptor
CreateConnectDatagram(const SocketAddress address)
{
    UniqueSocketDescriptor fd;
    if (!fd.CreateNonBlock(address.GetFamily(), SOCK_DGRAM, 0))
        throw MakeErrno("Failed to create socket");

    if (!fd.Connect(address)) {
        const int e = errno;
        char buffer[256];
        throw FormatErrno(e, "Failed to connect to %s",
                          ToString(buffer, sizeof(buffer), address));
    }

    return fd;
}

AccessLogGlue *
AccessLogGlue::Create(const AccessLogConfig &config,
                      const UidGid *user)
{
    switch (config.type) {
    case AccessLogConfig::Type::DISABLED:
        return nullptr;

    case AccessLogConfig::Type::INTERNAL:
        return new AccessLogGlue(config, nullptr);

    case AccessLogConfig::Type::SEND:
        return new AccessLogGlue(config,
                                 new LogClient(CreateConnectDatagram(config.send_to)));

    case AccessLogConfig::Type::EXECUTE:
        {
            auto lp = log_launch(config.command.c_str(), user);
            assert(lp.fd.IsDefined());

            return new AccessLogGlue(config, new LogClient(std::move(lp.fd)));
        }
    }

    assert(false);
    gcc_unreachable();
}

void
AccessLogGlue::Log(const AccessLogDatagram &d)
{
    if (!config.ignore_localhost_200.empty() &&
        d.http_uri != nullptr &&
        d.http_uri == config.ignore_localhost_200 &&
        d.host != nullptr &&
        strcmp(d.host, "localhost") == 0 &&
        d.http_status == HTTP_STATUS_OK)
        return;

    if (client != nullptr)
        client->Send(d);
    else
        LogOneLine(d);
}

/**
 * Extract the right-most item of a comma-separated list, such as an
 * X-Forwarded-For header value.  Returns the remaining string and the
 * right-most item as a std::pair.
 */
gcc_pure
static std::pair<StringView, StringView>
LastListItem(StringView list)
{
    const char *comma = (const char *)memrchr(list.data, ',', list.size);
    if (comma == nullptr) {
        list.Strip();
        if (list.IsEmpty())
            return std::make_pair(nullptr, nullptr);

        return std::make_pair("", list);
    }

    StringView value = list;
    value.MoveFront(comma + 1);
    value.Strip();

    list.size = comma - list.data;

    return std::make_pair(list, value);
}

/**
 * Extract the "real" remote host from an X-Forwarded-For request header.
 *
 * @param trust a list of trusted proxies
 */
gcc_pure
static StringView
GetRealRemoteHost(const char *xff, const std::set<std::string> &trust)
{
    StringView list(xff);
    StringView result(nullptr);

    while (true) {
        auto l = LastListItem(list);
        if (l.second.IsEmpty())
            /* list finished; return the last good address (even if
               it's a trusted proxy) */
            return result;

        result = l.second;
        if (trust.find(std::string(result.data, result.size)) == trust.end())
            /* this address is not a trusted proxy; return it */
            return result;

        list = l.first;
    }
}

void
AccessLogGlue::Log(HttpServerRequest &request, const char *site,
                   const char *referer, const char *user_agent,
                   http_status_t status, int64_t content_length,
                   uint64_t bytes_received, uint64_t bytes_sent,
                   std::chrono::steady_clock::duration duration)
{
    assert(http_method_is_valid(request.method));
    assert(http_status_is_valid(status));

    const char *remote_host = request.remote_host;
    std::string buffer;

    if (remote_host != nullptr &&
        !config.trust_xff.empty() &&
        config.trust_xff.find(remote_host) != config.trust_xff.end()) {
        const char *xff = request.headers.Get("x-forwarded-for");
        if (xff != nullptr) {
            auto r = GetRealRemoteHost(xff, config.trust_xff);
            if (!r.IsNull()) {
                buffer.assign(r.data, r.size);
                remote_host = buffer.c_str();
            }
        }
    }

    const AccessLogDatagram d(std::chrono::system_clock::now(),
                              request.method, request.uri,
                              remote_host,
                              request.headers.Get("host"),
                              site,
                              referer, user_agent,
                              status, content_length,
                              bytes_received, bytes_sent,
                              duration);
    Log(d);
}
