/*
 * test-bootstrap-job.c - Unit tests for bootstrap_job and bootout_job.
 *
 * Covers bootout/bootstrap/job_is_loaded argument guards and errmsg.
 * Does not require a live successful bootstrap.
 */

#include "bootstrap-job.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <unistd.h>

#define TEST_ASSERT(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return (1); \
	} \
} while (0)

/*
 * A NULL label is rejected before any XPC send.
 */
static int
test_bootout_null_label(void)
{
	int rc;

	errno = 0;
	rc = bootout_job(MACH_PORT_NULL, 501, NULL);
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(errno == EINVAL);
	TEST_ASSERT(bootstrap_job_errmsg() != NULL);
	TEST_ASSERT(bootstrap_job_errmsg()[0] != '\0');
	return (0);
}

/*
 * An empty label is rejected before any XPC send.
 */
static int
test_bootout_empty_label(void)
{
	int rc;

	errno = 0;
	rc = bootout_job(MACH_PORT_NULL, 501, "");
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(errno == EINVAL);
	TEST_ASSERT(bootstrap_job_errmsg() != NULL);
	TEST_ASSERT(bootstrap_job_errmsg()[0] != '\0');
	return (0);
}

/*
 * A missing service must fail without crashing (no live success required).
 */
static int
test_bootout_missing_label(void)
{
	int rc;

	rc = bootout_job(bootstrap_port, getuid(),
	    "com.xspawn.unit.missing");
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(bootstrap_job_errmsg() != NULL);
	TEST_ASSERT(bootstrap_job_errmsg()[0] != '\0');
	return (0);
}

/*
 * A NULL plist path is rejected before any XPC send.
 */
static int
test_bootstrap_null_path(void)
{
	int rc;

	rc = bootstrap_job(MACH_PORT_NULL, 501, NULL);
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(bootstrap_job_errmsg() != NULL);
	TEST_ASSERT(bootstrap_job_errmsg()[0] != '\0');
	return (0);
}

/*
 * An empty plist path is rejected before any XPC send.
 */
static int
test_bootstrap_empty_path(void)
{
	int rc;

	rc = bootstrap_job(MACH_PORT_NULL, 501, "");
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(bootstrap_job_errmsg() != NULL);
	TEST_ASSERT(bootstrap_job_errmsg()[0] != '\0');
	return (0);
}

/*
 * A missing plist must fail without crashing (no live success required).
 */
static int
test_bootstrap_missing_plist(void)
{
	int rc;

	rc = bootstrap_job(bootstrap_port, getuid(),
	    "/tmp/xspawn-no-dir/no.plist");
	TEST_ASSERT(rc == -1);
	return (0);
}

/*
 * A NULL label is rejected before any 708 send.
 */
static int
test_loaded_null_label(void)
{
	int rc;

	errno = 0;
	rc = job_is_loaded(MACH_PORT_NULL, 501, NULL);
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(errno == EINVAL);
	TEST_ASSERT(bootstrap_job_errmsg()[0] != '\0');
	return (0);
}

/*
 * An empty label is rejected before any 708 send.
 */
static int
test_loaded_empty_label(void)
{
	int rc;

	errno = 0;
	rc = job_is_loaded(MACH_PORT_NULL, 501, "");
	TEST_ASSERT(rc == -1);
	TEST_ASSERT(errno == EINVAL);
	return (0);
}

/*
 * A label that is not in gui or user is free (0).
 */
static int
test_loaded_missing_label(void)
{
	int rc;

	rc = job_is_loaded(bootstrap_port, getuid(),
	    "com.xspawn.unit.not-loaded");
	TEST_ASSERT(rc == 0);
	return (0);
}

int
main(void)
{
	if (test_bootout_null_label() != 0)
		exit(1);
	if (test_bootout_empty_label() != 0)
		exit(1);
	if (test_bootout_missing_label() != 0)
		exit(1);
	if (test_bootstrap_null_path() != 0)
		exit(1);
	if (test_bootstrap_empty_path() != 0)
		exit(1);
	if (test_bootstrap_missing_plist() != 0)
		exit(1);
	if (test_loaded_null_label() != 0)
		exit(1);
	if (test_loaded_empty_label() != 0)
		exit(1);
	if (test_loaded_missing_label() != 0)
		exit(1);
	printf("PASS\n");
	return (0);
}
