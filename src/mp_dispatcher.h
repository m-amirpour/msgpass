#ifndef MP_DISPATCHER_H
#define MP_DISPATCHER_H

#include "mp_protocol.h"
#include "os/os_socket.h"

/*
 * Take a decoded request, execute it, and send the encoded response
 * back on the given socket. Knows nothing about how the connection
 * was accepted or managed.
 */
int mp_dispatch(mp_socket_t fd, const mp_request_t *req);

#endif /* MP_DISPATCHER_H */