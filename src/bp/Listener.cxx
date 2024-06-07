// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Listener.hxx"
#include "LConfig.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "PrometheusExporter.hxx"
#include "pool/UniquePtr.hxx"
#include "ssl/Factory.hxx"
#include "ssl/Filter.hxx"
#include "ssl/CertCallback.hxx"
#include "ssl/AlpnProtos.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/SocketAddress.hxx"
#include "io/Logger.hxx"

#ifdef HAVE_AVAHI
#include "lib/avahi/Service.hxx"
#include "lib/avahi/Publisher.hxx"

inline std::unique_ptr<Avahi::Service>
BpListener::MakeAvahiService(const BpListenerConfig &config) const noexcept
{
	if (config.zeroconf_service.empty())
		return {};

	/* ask the kernel for the effective address via getsockname(),
	   because it may have changed, e.g. if the kernel has
	   selected a port for us */
	if (const auto local_address = GetLocalAddress();
	    local_address.IsDefined())
		return std::make_unique<Avahi::Service>(config.zeroconf_service.c_str(),
							config.GetZeroconfInterface(), local_address,
							config.v6only);

	return {};
}

#endif // HAVE_AVAHI

static std::unique_ptr<SslFactory>
MakeSslFactory(const BpListenerConfig &config)
{
	if (!config.ssl)
		return nullptr;

	auto ssl_factory = std::make_unique<SslFactory>(config.ssl_config, nullptr);
	// TODO: call SetSessionIdContext()

#ifdef HAVE_NGHTTP2
	ssl_factory->AddAlpn(alpn_http_any);
#endif

	return ssl_factory;
}

BpListener::BpListener(BpInstance &_instance,
		       TaggedHttpStats &_http_stats,
		       AccessLogGlue *_access_logger,
		       std::shared_ptr<TranslationService> _translation_service,
		       const BpListenerConfig &config,
		       UniqueSocketDescriptor _socket)
	:instance(_instance),
	 http_stats(_http_stats),
	 access_logger(_access_logger),
	 translation_service(_translation_service),
	 prometheus_exporter(config.handler == BpListenerConfig::Handler::PROMETHEUS_EXPORTER
			     ? new BpPrometheusExporter(instance)
			     : nullptr),
	 tag(config.tag.empty() ? nullptr : config.tag.c_str()),
	 auth_alt_host(config.auth_alt_host),
	 access_logger_only_errors(config.access_logger_only_errors),
	 listener(instance.root_pool, instance.event_loop,
		  MakeSslFactory(config),
		  *this, std::move(_socket))
#ifdef HAVE_AVAHI
	, avahi_service(MakeAvahiService(config))
#endif
{
#ifdef HAVE_AVAHI
	if (avahi_service)
		instance.GetAvahiPublisher().AddService(*avahi_service);
#endif
}

BpListener::~BpListener() noexcept
{
#ifdef HAVE_AVAHI
	if (avahi_service)
		instance.GetAvahiPublisher().RemoveService(*avahi_service);
#endif
}

#ifdef HAVE_AVAHI

void
BpListener::SetZeroconfVisible(bool _visible) noexcept
{
	assert(avahi_service);

	if (avahi_service->visible == _visible)
		return;

	avahi_service->visible = _visible;
	instance.GetAvahiPublisher().UpdateServices();
}

#endif

void
BpListener::OnFilteredSocketConnect(PoolPtr pool,
				    UniquePoolPtr<FilteredSocket> socket,
				    SocketAddress address,
				    const SslFilter *ssl_filter) noexcept
{
	new_connection(std::move(pool), instance, *this,
		       prometheus_exporter.get(),
		       std::move(socket), ssl_filter,
		       address);
}

void
BpListener::OnFilteredSocketError(std::exception_ptr ep) noexcept
{
	LogConcat(2, "listener", ep);
}
