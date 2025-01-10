// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "Connection.hxx"
#include "Error.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/GetHandler.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ListenChildStock.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/WithPoolDisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/StringList.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

class FcgiStock final : StockClass, ListenChildStockClass {
	PoolPtr pool{pool_new_dummy(nullptr, "FcgiStock")};
	StockMap hstock;
	ChildStockMap child_stock;

	class CreateRequest final : StockGetHandler, Cancellable {
		CreateStockItem create;
		StockGetHandler &handler;
		CancellablePointer cancel_ptr;

	public:
		CreateRequest(const CreateStockItem &_create,
			      StockGetHandler &_handler) noexcept
			:create(_create), handler(_handler) {}

		void Start(StockMap &child_stock_map,
			   StockRequest &&request,
			   CancellablePointer &caller_cancel_ptr) noexcept {
			caller_cancel_ptr = *this;
			child_stock_map.Get(create.GetStockName(),
					    std::move(request),
					    *this, cancel_ptr);
		}

	private:
		/* virtual methods from class StockGetHandler */
		void OnStockItemReady(StockItem &item) noexcept override;
		void OnStockItemError(std::exception_ptr error) noexcept override;

		/* virtual methods from class Cancellable */
		void Cancel() noexcept override {
			cancel_ptr.Cancel();
			delete this;
		}
	};

public:
	FcgiStock(unsigned limit, unsigned max_idle,
		  EventLoop &event_loop, SpawnService &spawn_service,
		  ListenStreamStock *listen_stream_stock,
		  SocketDescriptor _log_socket,
		  const ChildErrorLogOptions &_log_options) noexcept;

	~FcgiStock() noexcept {
		/* this one must be cleared before #child_stock; FadeAll()
		   calls ClearIdle(), so this method is the best match for
		   what we want to do (though a kludge) */
		hstock.FadeAll();
	}

	EventLoop &GetEventLoop() const noexcept {
		return hstock.GetEventLoop();
	}

	void Get(const ChildOptions &options,
		 const char *executable_path,
		 std::span<const char *const> args,
		 unsigned parallelism,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	void FadeAll() noexcept {
		hstock.FadeAll();
		child_stock.GetStockMap().FadeAll();
	}

	void FadeTag(std::string_view tag) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;

	/* virtual methods from class ChildStockClass */
	StockRequest PreserveRequest(StockRequest request) noexcept override;

	bool WantStderrFd(const void *info) const noexcept override;
	bool WantStderrPond(const void *info) const noexcept override;

	unsigned GetChildBacklog(const void *) const noexcept override {
		return 4;
	}

	std::string_view GetChildTag(const void *info) const noexcept override;
	void PrepareChild(const void *info, PreparedChildProcess &p,
			  FdHolder &close_fds) override;

	/* virtual methods from class ChildStockMapClass */
	std::size_t GetChildLimit(const void *request,
				  std::size_t _limit) const noexcept override;
	Event::Duration GetChildClearInterval(const void *info) const noexcept override;

	/* virtual methods from class ListenChildStockClass */
	void PrepareListenChild(const void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p,
				FdHolder &close_fds) override;
};

/*
 * child_stock class
 *
 */

std::size_t
FcgiStock::GetChildLimit(const void *request,
			 std::size_t _limit) const noexcept
{
	const auto &params = *(const CgiChildParams *)request;
	if (params.parallelism > 0)
		return params.parallelism;

	return _limit;
}

Event::Duration
FcgiStock::GetChildClearInterval(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(10)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

StockRequest
FcgiStock::PreserveRequest(StockRequest request) noexcept
{
	const auto &src = *reinterpret_cast<const CgiChildParams *>(request.get());
	return WithPoolDisposablePointer<CgiChildParams>::New(pool_new_linear(pool, "CgiChildParams", 4096), src);
}

bool
FcgiStock::WantStderrFd(const void *) const noexcept
{
	return true;
}

bool
FcgiStock::WantStderrPond(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;
	return params.options.stderr_pond;
}

std::string_view
FcgiStock::GetChildTag(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.tag;
}

void
FcgiStock::PrepareChild(const void *info, PreparedChildProcess &p,
			FdHolder &close_fds)
{
	const auto &params = *(const CgiChildParams *)info;
	const ChildOptions &options = params.options;

	/* the FastCGI protocol defines a channel for stderr, so we could
	   close its "real" stderr here, but many FastCGI applications
	   don't use the FastCGI protocol to send error messages, so we
	   just keep it open */

	if (UniqueFileDescriptor null_fd;
	    null_fd.Open("/dev/null", O_WRONLY))
		p.stdout_fd = close_fds.Insert(std::move(null_fd));

	p.Append(params.executable_path);
	for (auto i : params.args)
		p.Append(i);

	options.CopyTo(p, close_fds);
}

void
FcgiStock::PrepareListenChild(const void *, UniqueSocketDescriptor fd,
			      PreparedChildProcess &p,
			      FdHolder &close_fds)
{
	p.stdin_fd = close_fds.Insert(std::move(fd).MoveToFileDescriptor());
}

void
FcgiStock::CreateRequest::OnStockItemReady(StockItem &item) noexcept
{
	auto &child = static_cast<ListenChildStockItem &>(item);

	try {
		auto *connection = new FcgiConnection(create, child, child.Connect());
		connection->InvokeCreateSuccess(handler);
	} catch (...) {
		child.Put(PutAction::DESTROY);

		try {
			std::throw_with_nested(FcgiClientError(FcgiClientErrorCode::REFUSED,
							       FmtBuffer<256>("Failed to connect to FastCGI server {:?}",
									      create.GetStockName())));
		} catch (...) {
			create.InvokeCreateError(handler, std::current_exception());
		}
	}

	delete this;
}

void
FcgiStock::CreateRequest::OnStockItemError(std::exception_ptr error) noexcept
{
	create.InvokeCreateError(handler, std::move(error));
	delete this;
}

/*
 * stock class
 *
 */

void
FcgiStock::Create(CreateStockItem c, StockRequest request,
		  StockGetHandler &handler,
		  [[maybe_unused]] CancellablePointer &cancel_ptr)
{
	[[maybe_unused]] auto &params = *(CgiChildParams *)request.get();
	assert(params.executable_path != nullptr);

	auto *create = new CreateRequest(c, handler);
	create->Start(child_stock.GetStockMap(), std::move(request), cancel_ptr);
}

/*
 * interface
 *
 */

inline
FcgiStock::FcgiStock(unsigned limit, unsigned max_idle,
		     EventLoop &event_loop, SpawnService &spawn_service,
		     ListenStreamStock *listen_stream_stock,
		     SocketDescriptor _log_socket,
		     const ChildErrorLogOptions &_log_options) noexcept
	:hstock(event_loop, *this, limit, max_idle,
		std::chrono::minutes(2)),
	 child_stock(event_loop, spawn_service,
		     listen_stream_stock,
		     *this,
		     _log_socket, _log_options,
		     limit, max_idle) {}

void
FcgiStock::FadeTag(std::string_view tag) noexcept
{
	hstock.FadeIf([tag](const StockItem &item){
		const auto &connection = (const FcgiConnection &)item;
		return StringListContains(connection.GetTag(), '\0', tag);
	});

	child_stock.FadeTag(tag);
}

FcgiStock *
fcgi_stock_new(unsigned limit, unsigned max_idle,
	       EventLoop &event_loop, SpawnService &spawn_service,
	       ListenStreamStock *listen_stream_stock,
	       SocketDescriptor log_socket,
	       const ChildErrorLogOptions &log_options) noexcept
{
	return new FcgiStock(limit, max_idle, event_loop,
			     spawn_service, listen_stream_stock,
			     log_socket, log_options);
}

void
fcgi_stock_free(FcgiStock *fcgi_stock) noexcept
{
	delete fcgi_stock;
}

EventLoop &
fcgi_stock_get_event_loop(const FcgiStock &fs) noexcept
{
	return fs.GetEventLoop();
}

void
fcgi_stock_fade_all(FcgiStock &fs) noexcept
{
	fs.FadeAll();
}

void
fcgi_stock_fade_tag(FcgiStock &fs, std::string_view tag) noexcept
{
	fs.FadeTag(tag);
}

inline void
FcgiStock::Get(const ChildOptions &options,
	       const char *executable_path,
	       std::span<const char *const> args,
	       unsigned parallelism,
	       StockGetHandler &handler,
	       CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;

	auto r = ToDeletePointer(new CgiChildParams(executable_path,
						    args, options,
						    parallelism, 0, false));
	const char *key = r->GetStockKey(*tpool);
	hstock.Get(key, std::move(r),
			handler, cancel_ptr);
}

void
fcgi_stock_get(FcgiStock *fcgi_stock,
	       const ChildOptions &options,
	       const char *executable_path,
	       std::span<const char *const> args,
	       unsigned parallelism,
	       StockGetHandler &handler,
	       CancellablePointer &cancel_ptr) noexcept
{
	fcgi_stock->Get(options, executable_path, args,
			parallelism,
			handler, cancel_ptr);
}

int
fcgi_stock_item_get_domain([[maybe_unused]] const StockItem &item) noexcept
{
	return AF_LOCAL;
}
