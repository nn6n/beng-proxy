/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_hold.hxx"
#include "istream_forward.hxx"

#include <assert.h>

class HoldIstream final : public ForwardIstream {
    bool input_eof = false;
    GError *input_error = nullptr;

public:
    HoldIstream(struct pool &p, struct istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<HoldIstream>::handler, this) {}

private:
    bool Check() {
        if (gcc_unlikely(input_eof)) {
            DestroyEof();
            return false;
        } else if (gcc_unlikely(input_error != nullptr)) {
            DestroyError(input_error);
            return false;
        } else
            return true;
    }

public:
    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        if (gcc_unlikely(input_eof))
            return 0;
        else if (gcc_unlikely(input_error != nullptr))
            return -1;

        return ForwardIstream::GetAvailable(partial);
    }

    void Read() override {
        if (gcc_likely(Check()))
            ForwardIstream::Read();
    }

    int AsFd() override {
        return Check()
            ? ForwardIstream::AsFd()
            : -1;
    }

    void Close() override {
        if (input_eof)
            Destroy();
        else if (input_error != nullptr) {
            /* the handler is not interested in the error */
            g_error_free(input_error);
            Destroy();
        } else {
            /* the input object is still there */
            ForwardIstream::Close();
        }
    }

    /* handler */

    size_t OnData(const void *data, size_t length) {
        return HasHandler() ? ForwardIstream::OnData(data, length) : 0;
    }

    ssize_t OnDirect(enum istream_direct type, int fd, size_t max_length) {
        return HasHandler()
            ? ForwardIstream::OnDirect(type, fd, max_length)
            : ISTREAM_RESULT_BLOCKING;
    }

    void OnEof() {
        assert(!input_eof);
        assert(input_error == nullptr);

        if (HasHandler())
            ForwardIstream::OnEof();
        else
            /* queue the eof() call */
            input_eof = true;
    }

    void OnError(GError *error) {
        assert(!input_eof);
        assert(input_error == nullptr);

        if (HasHandler())
            ForwardIstream::OnError(error);
        else
            /* queue the abort() call */
            input_error = error;
    }
};

struct istream *
istream_hold_new(struct pool *pool, struct istream *input)
{
    return NewIstream<HoldIstream>(*pool, *input);
}
