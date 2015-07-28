/*
 * C++ wrappers for libevent using std::function.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef FUNCTIONAL_EVENT_HXX
#define FUNCTIONAL_EVENT_HXX

#include "Event.hxx"

#include <functional>

class SignalEvent {
    Event event;

    const std::function<void()> handler;

public:
    SignalEvent(int sig, std::function<void()> _handler)
        :handler(_handler) {
        event.SetSignal(sig, Callback, this);
        event.Add();
    }

    ~SignalEvent() {
        Delete();
    }

    void Delete() {
        event.Delete();
    }

private:
    static void Callback(int fd, short event, void *ctx);
};

#endif
