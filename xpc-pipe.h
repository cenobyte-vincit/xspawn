/*
 * xpc-pipe.h - Private libxpc pipe symbols not in the SDK.
 *
 * Declares the pipe create and interface-routine entry points used to
 * send launchd bootstrap messages. Not present in public headers.
 *
 */

#ifndef XPC_PIPE_H
#define XPC_PIPE_H

#include <stdint.h>

#include <mach/mach.h>
#include <xpc/xpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *xpc_pipe_t;

extern xpc_pipe_t xpc_pipe_create_from_port(mach_port_t, uint64_t);

extern int _xpc_pipe_interface_routine(xpc_pipe_t, int, xpc_object_t,
    xpc_object_t *, uint64_t);

#ifdef __cplusplus
}
#endif

#endif /* XPC_PIPE_H */
