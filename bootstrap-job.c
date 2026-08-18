/*
 * bootstrap-job.c - Private XPC bootstrap of a launchd job.
 *
 * Builds the live load, bootout, or occupancy-print message and
 * sends it over a pipe created from the domain Mach port. launchd
 * becomes the exec-time parent. Bootout is by label (descriptor
 * 801). Occupancy is descriptor 708 in gui and user.
 *
 */

#include "bootstrap-job.h"

#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "xpc-pipe.h"

/*
 * macOS 26.6.1 (25G76). One wire for x86_64 and arm64.
 * x86_64 lldb on launchctl bootstrap gui/<uid> <plist>:
 *   xpc_pipe_create_from_port  rsi=4
 *   _xpc_pipe_interface_routine  rsi=800 r8=6
 *   dict: handle=uid type=8 paths=[plist] by-cli=true
 * Bootout (Phase 3 x86_64 capture; same pipe flags):
 *   _xpc_pipe_interface_routine  rsi=801 r8=6
 *   dict: handle=uid type=8 name=label no-einprogress wait
 * Occupancy / print (2026-08-16 x86_64 lldb on launchctl print):
 *   _xpc_pipe_interface_routine  rsi=708 r8=6
 *   dict: handle=uid type=8|2 name=label  (no shmem for occupancy)
 *   reply error 22 = present, 113 = absent; pipe rc 0
 * arm64 client oracle: same constants succeed; no retry.
 */
#define PIPE_CREATE_FLAGS		4ull
#define PIPE_INTERFACE_FLAGS		6ull
#define BOOTSTRAP_ROUTINE_DESCRIPTOR	800	/* 0x320; pipe arg, not dict */
#define BOOTOUT_ROUTINE_DESCRIPTOR	801	/* 0x321; Phase 3 live dict */
#define PRINT_ROUTINE_DESCRIPTOR	708	/* 0x2c4; occupancy / print */
#define PRINT_ERROR_LOADED		22	/* exists; no shmem */
#define PRINT_ERROR_MISSING		113	/* not in that domain */
#define BOOTSTRAP_DOMAIN_TYPE_GUI	8ull
#define BOOTSTRAP_DOMAIN_TYPE_USER	2ull

static char errmsg[256];

/*
 * Replace the static last-failure buffer. Valid until the next call.
 */
static void
set_errmsg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(errmsg, sizeof(errmsg), fmt, ap);
	va_end(ap);
}

/*
 * Format a failure from the pipe return code and reply keys.
 */
static void
set_fail_errmsg(int rc, xpc_object_t reply)
{
	const char *fault;
	int64_t berr, err;

	fault = NULL;
	berr = 0;
	err = 0;
	if (reply != NULL) {
		fault = xpc_dictionary_get_string(reply, "xpc-fault");
		err = xpc_dictionary_get_int64(reply, "error");
		berr = xpc_dictionary_get_int64(reply, "bootstrap-error");
	}
	if (fault != NULL)
		set_errmsg("xpc-fault: %s", fault);
	else if (rc == 0 && err != 0)
		set_errmsg("launchd error: %lld", (long long)err);
	else if (rc == 0 && berr != 0)
		set_errmsg("bootstrap-error: %lld", (long long)berr);
	else
		set_errmsg("_xpc_pipe_interface_routine failed: %d", rc);
}

/*
 * Return 1 if the pipe call and reply are a launchd success.
 */
static int
reply_ok(int rc, xpc_object_t reply)
{
	const char *fault;
	int64_t berr, err;

	if (rc != 0)
		return (0);
	if (reply == NULL)
		return (1);
	fault = xpc_dictionary_get_string(reply, "xpc-fault");
	if (fault != NULL)
		return (0);
	err = xpc_dictionary_get_int64(reply, "error");
	if (err != 0)
		return (0);
	/* Live launchd reports missing/bad plists as bootstrap-error. */
	berr = xpc_dictionary_get_int64(reply, "bootstrap-error");
	if (berr != 0)
		return (0);
	return (1);
}

/*
 * Build the bootstrap load dictionary (no subsystem/routine keys).
 */
static xpc_object_t
build_load_msg(uid_t uid, const char *plist_path)
{
	xpc_object_t msg, paths;

	msg = xpc_dictionary_create(NULL, NULL, 0);
	if (msg == NULL)
		errx(1, "malloc");
	xpc_dictionary_set_uint64(msg, "handle", (uint64_t)uid);
	xpc_dictionary_set_uint64(msg, "type", BOOTSTRAP_DOMAIN_TYPE_GUI);
	paths = xpc_array_create(NULL, 0);
	if (paths == NULL)
		errx(1, "malloc");
	xpc_array_set_string(paths, XPC_ARRAY_APPEND, plist_path);
	xpc_dictionary_set_value(msg, "paths", paths);
	xpc_release(paths);
	xpc_dictionary_set_bool(msg, "by-cli", true);
	return (msg);
}

/*
 * Send msg with descriptor over a pipe from domain_port.
 * Returns 0 on launchd success, -1 on failure (errmsg set).
 */
static int
pipe_send(mach_port_t domain_port, int descriptor, xpc_object_t msg)
{
	xpc_object_t reply;
	xpc_pipe_t pipe;
	int ok, rc;

	pipe = xpc_pipe_create_from_port(domain_port, PIPE_CREATE_FLAGS);
	if (pipe == NULL) {
		set_errmsg("xpc_pipe_create_from_port failed");
		return (-1);
	}
	reply = NULL;
	rc = _xpc_pipe_interface_routine(pipe, descriptor, msg, &reply,
	    PIPE_INTERFACE_FLAGS);
	ok = reply_ok(rc, reply);
	if (!ok)
		set_fail_errmsg(rc, reply);
	if (reply != NULL)
		xpc_release(reply);
	xpc_release(pipe);
	return (ok ? 0 : -1);
}

/*
 * Submit plist_path to the gui domain over the private launchd pipe.
 */
int
bootstrap_job(mach_port_t domain_port, uid_t uid, const char *plist_path)
{
	xpc_object_t msg;
	int rc;

	errmsg[0] = '\0';
	if (plist_path == NULL || *plist_path == '\0') {
		errno = EINVAL;
		set_errmsg("plist path is required");
		return (-1);
	}
	msg = build_load_msg(uid, plist_path);
	rc = pipe_send(domain_port, BOOTSTRAP_ROUTINE_DESCRIPTOR, msg);
	xpc_release(msg);
	return (rc);
}

/*
 * Build the bootout dictionary (no subsystem/routine keys).
 */
static xpc_object_t
build_bootout_msg(uid_t uid, const char *label)
{
	xpc_object_t msg;

	msg = xpc_dictionary_create(NULL, NULL, 0);
	if (msg == NULL)
		errx(1, "malloc");
	xpc_dictionary_set_uint64(msg, "handle", (uint64_t)uid);
	xpc_dictionary_set_uint64(msg, "type", BOOTSTRAP_DOMAIN_TYPE_GUI);
	xpc_dictionary_set_string(msg, "name", label);
	xpc_dictionary_set_bool(msg, "no-einprogress", true);
	xpc_dictionary_set_bool(msg, "wait", true);
	return (msg);
}

/*
 * Unload label from the gui domain over the private launchd pipe.
 */
int
bootout_job(mach_port_t domain_port, uid_t uid, const char *label)
{
	xpc_object_t msg;
	int rc;

	errmsg[0] = '\0';
	if (label == NULL || *label == '\0') {
		errno = EINVAL;
		set_errmsg("label is required");
		return (-1);
	}
	msg = build_bootout_msg(uid, label);
	rc = pipe_send(domain_port, BOOTOUT_ROUTINE_DESCRIPTOR, msg);
	xpc_release(msg);
	return (rc);
}

/*
 * Build a 708 occupancy dict (no shmem, no subsystem/routine).
 */
static xpc_object_t
build_print_msg(uid_t uid, uint64_t typ, const char *label)
{
	xpc_object_t msg;

	msg = xpc_dictionary_create(NULL, NULL, 0);
	if (msg == NULL)
		errx(1, "malloc");
	xpc_dictionary_set_uint64(msg, "handle", (uint64_t)uid);
	xpc_dictionary_set_uint64(msg, "type", typ);
	xpc_dictionary_set_string(msg, "name", label);
	return (msg);
}

/*
 * Probe one domain. Return 1 if loaded, 0 if absent, -1 on failure.
 */
static int
probe_domain(mach_port_t domain_port, uid_t uid, uint64_t typ,
    const char *label)
{
	xpc_object_t msg, reply;
	xpc_pipe_t pipe;
	const char *fault;
	int64_t errv;
	uint64_t nbytes;
	int rc;

	msg = build_print_msg(uid, typ, label);
	pipe = xpc_pipe_create_from_port(domain_port, PIPE_CREATE_FLAGS);
	if (pipe == NULL) {
		xpc_release(msg);
		set_errmsg("xpc_pipe_create_from_port failed");
		return (-1);
	}
	reply = NULL;
	rc = _xpc_pipe_interface_routine(pipe, PRINT_ROUTINE_DESCRIPTOR,
	    msg, &reply, PIPE_INTERFACE_FLAGS);
	xpc_release(msg);
	xpc_release(pipe);
	if (rc != 0) {
		if (reply != NULL)
			xpc_release(reply);
		set_errmsg("_xpc_pipe_interface_routine failed: %d", rc);
		return (-1);
	}
	if (reply == NULL) {
		set_errmsg("print reply missing");
		return (-1);
	}
	fault = xpc_dictionary_get_string(reply, "xpc-fault");
	if (fault != NULL) {
		set_errmsg("xpc-fault: %s", fault);
		xpc_release(reply);
		return (-1);
	}
	errv = xpc_dictionary_get_int64(reply, "error");
	nbytes = xpc_dictionary_get_uint64(reply, "bytes-written");
	xpc_release(reply);
	if (nbytes > 0 || errv == PRINT_ERROR_LOADED)
		return (1);
	if (errv == PRINT_ERROR_MISSING)
		return (0);
	set_errmsg("print error: %lld", (long long)errv);
	return (-1);
}

/*
 * Return 1 if label is in gui or user, 0 if free, -1 on probe failure.
 */
int
job_is_loaded(mach_port_t domain_port, uid_t uid, const char *label)
{
	int loaded;

	errmsg[0] = '\0';
	if (label == NULL || *label == '\0') {
		errno = EINVAL;
		set_errmsg("label is required");
		return (-1);
	}
	loaded = probe_domain(domain_port, uid, BOOTSTRAP_DOMAIN_TYPE_GUI,
	    label);
	if (loaded != 0)
		return (loaded);
	return (probe_domain(domain_port, uid, BOOTSTRAP_DOMAIN_TYPE_USER,
	    label));
}

/*
 * Return the last failure string, valid until the next call.
 */
const char *
bootstrap_job_errmsg(void)
{
	return (errmsg);
}
