#ifndef MP_DISPATCHER_H
#define MP_DISPATCHER_H

/*
 * The dispatcher sits between the server engine and the executor.
 * It takes a decoded request, runs it, and sends the encoded response.
 * It knows about sockets but not about how the connection was accepted.
 */

#include "mp_protocol.h"
#include "os/os_socket.h"

int mp_dispatch(mp_socket_t fd, const mp_request_t *req);

#endif /* MP_DISPATCHER_H */