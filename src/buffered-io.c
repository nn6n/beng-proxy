/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered-io.h"
#include "fifo-buffer.h"

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

ssize_t
read_to_buffer(int fd, struct fifo_buffer *buffer, size_t length)
{
    void *dest;
    size_t max_length;
    ssize_t nbytes;

    assert(fd >= 0);
    assert(buffer != NULL);

    dest = fifo_buffer_write(buffer, &max_length);
    if (dest == NULL)
        return -2;

    if (length > max_length)
        length = max_length;

    nbytes = read(fd, dest, length);
    if (nbytes > 0)
        fifo_buffer_append(buffer, (size_t)nbytes);

    return nbytes;
}

ssize_t
write_from_buffer(int fd, struct fifo_buffer *buffer)
{
    const void *data;
    size_t length;
    ssize_t nbytes;

    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return -2;

    nbytes = write(fd, data, length);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return length;

    fifo_buffer_consume(buffer, (size_t)nbytes);
    return (ssize_t)length - nbytes;
}

ssize_t
recv_to_buffer(int fd, struct fifo_buffer *buffer, size_t length)
{
    void *dest;
    size_t max_length;
    ssize_t nbytes;

    assert(fd >= 0);
    assert(buffer != NULL);

    dest = fifo_buffer_write(buffer, &max_length);
    if (dest == NULL)
        return -2;

    if (length > max_length)
        length = max_length;

    nbytes = recv(fd, dest, length, MSG_DONTWAIT);
    if (nbytes > 0)
        fifo_buffer_append(buffer, (size_t)nbytes);

    return nbytes;
}

ssize_t
send_from_buffer(int fd, struct fifo_buffer *buffer)
{
    const void *data;
    size_t length;
    ssize_t nbytes;

    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return -2;

    nbytes = send(fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0 && errno != EAGAIN)
        return -1;

    if (nbytes <= 0)
        return length;

    fifo_buffer_consume(buffer, (size_t)nbytes);
    return (ssize_t)length - nbytes;
}
