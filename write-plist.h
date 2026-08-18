/*
 * write-plist.h - Write a temporary binary launchd.plist(5).
 *
 * Public API for creating a well-formed launchd job plist in a
 * mkdtemp directory. Callers pass a buffer that receives the exclusive
 * output path and must call write_plist_cleanup() when finished.
 */

#ifndef WRITE_PLIST_H
#define WRITE_PLIST_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Write a temp launchd plist for label + argv.
 * On success: create $TMPDIR/XXXXXX/XXXXXX.plist
 * (TMPDIR must be absolute; otherwise /tmp) as a binary plist
 * (bplist00) and fill path_buf with that path. Return 0.
 * On failure: return -1 (remove a partial file and directory).
 * keepalive non-zero: KeepAlive true.
 * keepalive zero: RunAtLoad true and LaunchOnlyOnce true.
 * stdout_path / stderr_path may be NULL (omit those keys).
 * dump may be NULL (no print). Non-NULL: write the absolute path
 * and an XML copy of the job dictionary to dump, serialised from
 * the in-memory dict immediately before the binary write.
 * Do NOT write an XPCService key.
 */
int write_plist(char *, size_t, const char *, char *const *, size_t,
    const char *, const char *, int, FILE *);

/*
 * Print path and an XML copy of an on-disk plist to fp.
 * Reads the file once into memory, then serialises XML.
 * Returns 0, or -1.
 */
int print_plist_file(FILE *, const char *);

/*
 * Copy the Label string from an on-disk plist into buf.
 * Returns 0, or -1.
 */
int plist_copy_label(const char *, char *, size_t);

/*
 * Unlink plist_path and rmdir its parent. NULL or empty is a no-op.
 * Idempotent. Safe if the file or directory is already gone.
 */
void write_plist_cleanup(const char *);

#ifdef __cplusplus
}
#endif

#endif /* WRITE_PLIST_H */
