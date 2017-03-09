/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_CONNECTION_HXX
#define TRAFO_CONNECTION_HXX

#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/DynamicFifoBuffer.hxx"
#include "AllocatedRequest.hxx"

#include <string>

#include <stdint.h>

class TrafoResponse;
class TrafoListener;
class TrafoHandler;

class TrafoConnection {
    TrafoListener &listener;
    TrafoHandler &handler;

    const UniqueSocketDescriptor fd;
    SocketEvent read_event, write_event;

    enum class State {
        INIT,
        REQUEST,
        PROCESSING,
        RESPONSE,
    } state;

    DynamicFifoBuffer<uint8_t> input;

    AllocatedTrafoRequest request;

    uint8_t *response;

    WritableBuffer<uint8_t> output;

public:
    TrafoConnection(EventLoop &event_loop,
                    TrafoListener &_listener, TrafoHandler &_handler,
                    UniqueSocketDescriptor &&_fd);
    ~TrafoConnection();

    /**
     * For TrafoListener::connections.
     */
    bool operator==(const TrafoConnection &other) const {
        return fd == other.fd;
    }

    void SendResponse(TrafoResponse &&response);

private:
    void TryRead();
    void OnReceived();
    void OnPacket(unsigned cmd, const void *payload, size_t length);

    void TryWrite();

    void ReadEventCallback(unsigned) {
        TryRead();
    }

    void WriteEventCallback(unsigned) {
        TryWrite();
    }
};

#endif
