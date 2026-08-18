/*
 * bootstrap-job.h - Submit a launchd job via the private XPC pipe.
 *
 * bootstrap_job() loads a plist into the gui domain using the 25G76
 * live wire on x86_64 and arm64 (ARCHITECTURE.md). bootout_job()
 * unloads a gui-domain job by label (descriptor 801).
 * job_is_loaded() probes occupancy (descriptor 708) in gui and user.
 * bootstrap_job_errmsg() holds the last failure text.
 */

#ifndef BOOTSTRAP_JOB_H
#define BOOTSTRAP_JOB_H

#include <sys/types.h>

#include <mach/mach.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Submit plist_path to launchd via the private pipe (gui domain).
 * Returns 0 on XPC+launchd success, -1 on failure.
 */
int bootstrap_job(mach_port_t, uid_t, const char *);

/*
 * Unload label from the gui domain over the private pipe.
 * Returns 0 on XPC+launchd success, -1 on failure.
 */
int bootout_job(mach_port_t, uid_t, const char *);

/*
 * Return 1 if label is already installed in gui or user for uid,
 * 0 if neither domain has it, -1 on probe failure (errmsg set).
 * Descriptor 708, no shmem. Callers use this before write_plist()
 * so a taken label does not create the temp plist (a DFIR artefact).
 */
int job_is_loaded(mach_port_t, uid_t, const char *);

/*
 * Human-readable detail for the last bootstrap_job/bootout_job failure.
 * Valid until the next call.
 */
const char *bootstrap_job_errmsg(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOTSTRAP_JOB_H */
