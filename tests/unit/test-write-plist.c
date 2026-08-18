/*
 * test-write-plist.c - Unit tests for write_plist().
 *
 * Covers oneshot and keepalive plists, optional log paths, special
 * characters, temp-directory layout, cleanup, stdout dump (path +
 * XML from the in-memory dict), print_plist_file, and bad-argument
 * rejection. Parses the binary plist with CoreFoundation.
 */

#include "write-plist.h"

#include <sys/stat.h>

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>

/* Plist path from the last write_plist; cleaned on assert failure. */
static char g_cleanup[PATH_MAX];

#define TEST_ASSERT(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		write_plist_cleanup(g_cleanup); \
		g_cleanup[0] = '\0'; \
		return (1); \
	} \
} while (0)

/*
 * Build a unique reverse-DNS test label that includes the pid.
 */
static void
make_label(char *buf, size_t buflen, const char *suffix)
{
	(void)snprintf(buf, buflen, "com.test.writeplist.%d.%s",
	    (int)getpid(), suffix);
}

/*
 * Remember path for later write_plist_cleanup.
 */
static void
track_plist(const char *path)
{
	if (strlcpy(g_cleanup, path, sizeof(g_cleanup)) >= sizeof(g_cleanup))
		g_cleanup[0] = '\0';
}

/*
 * Unlink the tracked plist and rmdir its parent, if any.
 */
static void
cleanup_plist(void)
{
	write_plist_cleanup(g_cleanup);
	g_cleanup[0] = '\0';
}

/*
 * Return 0 if path is .../<6-char>/<6-char>.plist.
 */
static int
path_is_job_plist(const char *path)
{
	const char *slash;
	const char *dir;
	size_t n;

	if (path == NULL)
		return (-1);
	n = strlen(path);
	if (n < 13 || strcmp(path + n - 6, ".plist") != 0)
		return (-1);
	slash = strrchr(path, '/');
	if (slash == NULL || slash == path)
		return (-1);
	if ((path + n) - (slash + 1) != 12)
		return (-1);
	dir = slash;
	while (dir > path && *(dir - 1) != '/')
		dir--;
	if (slash - dir != 6)
		return (-1);
	return (0);
}

/*
 * Copy the parent directory of path into dir. Returns 0, or -1.
 */
static int
parent_of(char *dir, size_t dir_len, const char *path)
{
	char *slash;

	if (strlcpy(dir, path, dir_len) >= dir_len)
		return (-1);
	slash = strrchr(dir, '/');
	if (slash == NULL)
		return (-1);
	*slash = '\0';
	return (0);
}

/*
 * Return 1 if the file starts with bplist00.
 */
static int
is_bplist(const char *path)
{
	unsigned char magic[8];
	FILE *fp;
	size_t n;

	fp = fopen(path, "r");
	if (fp == NULL)
		return (0);
	n = fread(magic, 1, sizeof(magic), fp);
	(void)fclose(fp);
	if (n != sizeof(magic))
		return (0);
	return (memcmp(magic, "bplist00", 8) == 0);
}

/*
 * Load a binary plist file as a dictionary. Caller CFReleases.
 */
static CFDictionaryRef
load_dict(const char *path)
{
	CFDataRef data;
	CFPropertyListRef plist;
	FILE *fp;
	unsigned char *buf;
	long n;
	size_t got;

	fp = fopen(path, "r");
	if (fp == NULL)
		return (NULL);
	if (fseek(fp, 0, SEEK_END) != 0) {
		(void)fclose(fp);
		return (NULL);
	}
	n = ftell(fp);
	if (n <= 0) {
		(void)fclose(fp);
		return (NULL);
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		(void)fclose(fp);
		return (NULL);
	}
	buf = malloc((size_t)n);
	if (buf == NULL)
		errx(1, "malloc");
	got = fread(buf, 1, (size_t)n, fp);
	(void)fclose(fp);
	if (got != (size_t)n) {
		free(buf);
		return (NULL);
	}
	data = CFDataCreate(kCFAllocatorDefault, buf, (CFIndex)n);
	free(buf);
	if (data == NULL)
		return (NULL);
	plist = CFPropertyListCreateWithData(kCFAllocatorDefault, data,
	    kCFPropertyListImmutable, NULL, NULL);
	CFRelease(data);
	if (plist == NULL)
		return (NULL);
	if (CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
		CFRelease(plist);
		return (NULL);
	}
	return (plist);
}

/*
 * Return 1 if key maps to a CFString equal to want.
 */
static int
has_string(CFDictionaryRef d, CFStringRef key, const char *want)
{
	CFStringRef s;
	char buf[1024];

	s = CFDictionaryGetValue(d, key);
	if (s == NULL || CFGetTypeID(s) != CFStringGetTypeID())
		return (0);
	if (!CFStringGetCString(s, buf, sizeof(buf),
	    kCFStringEncodingUTF8))
		return (0);
	return (strcmp(buf, want) == 0);
}

/*
 * Return 1 if key maps to CFBoolean true.
 */
static int
has_true(CFDictionaryRef d, CFStringRef key)
{
	CFBooleanRef b;

	b = CFDictionaryGetValue(d, key);
	if (b == NULL || CFGetTypeID(b) != CFBooleanGetTypeID())
		return (0);
	return (CFBooleanGetValue(b));
}

/*
 * Return 1 if ProgramArguments[i] equals want.
 */
static int
has_arg(CFDictionaryRef d, CFIndex i, const char *want)
{
	CFArrayRef a;
	CFStringRef s;
	char buf[1024];

	a = CFDictionaryGetValue(d, CFSTR("ProgramArguments"));
	if (a == NULL || CFGetTypeID(a) != CFArrayGetTypeID())
		return (0);
	if (i < 0 || i >= CFArrayGetCount(a))
		return (0);
	s = CFArrayGetValueAtIndex(a, i);
	if (s == NULL || CFGetTypeID(s) != CFStringGetTypeID())
		return (0);
	if (!CFStringGetCString(s, buf, sizeof(buf),
	    kCFStringEncodingUTF8))
		return (0);
	return (strcmp(buf, want) == 0);
}

/*
 * Oneshot (keepalive=0, no logs): path, mode, required keys, no KeepAlive.
 */
static int
test_oneshot(void)
{
	CFDictionaryRef d;
	struct stat st;
	char dir[PATH_MAX];
	char path[PATH_MAX];
	char label[128];
	char *argv[] = { "/tmp/helloworld", "0", NULL };

	make_label(label, sizeof(label), "oneshot");
	g_cleanup[0] = '\0';

	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, NULL) == 0);
	track_plist(path);
	TEST_ASSERT(path_is_job_plist(path) == 0);
	TEST_ASSERT(stat(path, &st) == 0);
	TEST_ASSERT(S_ISREG(st.st_mode));
	TEST_ASSERT((st.st_mode & S_IWOTH) == 0);
	TEST_ASSERT(parent_of(dir, sizeof(dir), path) == 0);
	TEST_ASSERT(stat(dir, &st) == 0);
	TEST_ASSERT(S_ISDIR(st.st_mode));
	TEST_ASSERT((st.st_mode & S_IWOTH) == 0);
	TEST_ASSERT(is_bplist(path));
	d = load_dict(path);
	TEST_ASSERT(d != NULL);
	TEST_ASSERT(has_string(d, CFSTR("Label"), label));
	TEST_ASSERT(has_arg(d, 0, "/tmp/helloworld"));
	TEST_ASSERT(has_arg(d, 1, "0"));
	TEST_ASSERT(has_true(d, CFSTR("RunAtLoad")));
	TEST_ASSERT(has_true(d, CFSTR("LaunchOnlyOnce")));
	TEST_ASSERT(CFDictionaryGetValue(d, CFSTR("KeepAlive")) == NULL);
	TEST_ASSERT(CFDictionaryGetValue(d, CFSTR("XPCService")) == NULL);
	TEST_ASSERT(CFDictionaryGetValue(d,
	    CFSTR("StandardOutPath")) == NULL);
	TEST_ASSERT(CFDictionaryGetValue(d,
	    CFSTR("StandardErrorPath")) == NULL);
	CFRelease(d);

	cleanup_plist();
	return (0);
}

/*
 * Keepalive job: KeepAlive present, LaunchOnlyOnce absent.
 */
static int
test_keepalive(void)
{
	CFDictionaryRef d;
	char path[PATH_MAX];
	char label[128];
	char *argv[] = { "/tmp/helloworld", "20", NULL };

	make_label(label, sizeof(label), "keepalive");
	g_cleanup[0] = '\0';

	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 1, NULL) == 0);
	track_plist(path);
	TEST_ASSERT(is_bplist(path));
	d = load_dict(path);
	TEST_ASSERT(d != NULL);
	TEST_ASSERT(has_true(d, CFSTR("KeepAlive")));
	TEST_ASSERT(CFDictionaryGetValue(d,
	    CFSTR("LaunchOnlyOnce")) == NULL);
	TEST_ASSERT(CFDictionaryGetValue(d, CFSTR("XPCService")) == NULL);
	CFRelease(d);

	cleanup_plist();
	return (0);
}

/*
 * stdout_path and stderr_path become StandardOutPath / StandardErrorPath.
 */
static int
test_log_paths(void)
{
	CFDictionaryRef d;
	char path[PATH_MAX];
	char label[128];
	char *argv[] = { "/tmp/helloworld", "0", NULL };

	make_label(label, sizeof(label), "logs");
	g_cleanup[0] = '\0';

	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    "/tmp/out.log", "/tmp/err.log", 0, NULL) == 0);
	track_plist(path);
	d = load_dict(path);
	TEST_ASSERT(d != NULL);
	TEST_ASSERT(has_string(d, CFSTR("StandardOutPath"),
	    "/tmp/out.log"));
	TEST_ASSERT(has_string(d, CFSTR("StandardErrorPath"),
	    "/tmp/err.log"));
	CFRelease(d);

	cleanup_plist();
	return (0);
}

/*
 * Special characters in argv are stored as themselves, not XML entities.
 */
static int
test_special_chars(void)
{
	CFDictionaryRef d;
	char path[PATH_MAX];
	char label[128];
	char *argv[] = { "/tmp/helloworld", "a&b<c>d", NULL };

	make_label(label, sizeof(label), "special");
	g_cleanup[0] = '\0';

	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, NULL) == 0);
	track_plist(path);
	d = load_dict(path);
	TEST_ASSERT(d != NULL);
	TEST_ASSERT(has_arg(d, 1, "a&b<c>d"));
	CFRelease(d);

	cleanup_plist();
	return (0);
}

/*
 * write_plist_cleanup removes the file and the temp directory.
 */
static int
test_cleanup_removes_dir(void)
{
	char dir[PATH_MAX];
	char path[PATH_MAX];
	char label[128];
	char *argv[] = { "/tmp/helloworld", "0", NULL };

	make_label(label, sizeof(label), "cleanup");
	g_cleanup[0] = '\0';

	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, NULL) == 0);
	track_plist(path);
	TEST_ASSERT(parent_of(dir, sizeof(dir), path) == 0);
	TEST_ASSERT(access(path, F_OK) == 0);
	TEST_ASSERT(access(dir, F_OK) == 0);
	write_plist_cleanup(path);
	g_cleanup[0] = '\0';
	TEST_ASSERT(access(path, F_OK) != 0);
	TEST_ASSERT(access(dir, F_OK) != 0);
	write_plist_cleanup(path);
	write_plist_cleanup(NULL);
	write_plist_cleanup("");
	return (0);
}

/*
 * Reject NULL path_buf, path_len 0, NULL label, NULL argv, and argc 0.
 */
static int
test_bad_args(void)
{
	char path[PATH_MAX];
	char label[128];
	char *argv[] = { "/tmp/helloworld", "0", NULL };

	make_label(label, sizeof(label), "bad");
	g_cleanup[0] = '\0';
	path[0] = '\0';

	TEST_ASSERT(write_plist(NULL, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, NULL) == -1);
	TEST_ASSERT(write_plist(path, 0, label, argv, 2,
	    NULL, NULL, 0, NULL) == -1);
	TEST_ASSERT(path[0] == '\0');
	TEST_ASSERT(write_plist(path, sizeof(path), NULL, argv, 2,
	    NULL, NULL, 0, NULL) == -1);
	TEST_ASSERT(path[0] == '\0');
	TEST_ASSERT(write_plist(path, sizeof(path), label, NULL, 2,
	    NULL, NULL, 0, NULL) == -1);
	TEST_ASSERT(path[0] == '\0');
	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 0,
	    NULL, NULL, 0, NULL) == -1);
	TEST_ASSERT(path[0] == '\0');

	cleanup_plist();
	return (0);
}

/*
 * dump FILE receives the path line then XML; the on-disk file stays
 * binary. XML is serialised from the in-memory dict, not a re-read.
 */
static int
test_dump_xml(void)
{
	FILE *fp;
	char dump[8192];
	char path[PATH_MAX];
	char label[128];
	char *nl;
	char *argv[] = { "/tmp/helloworld", "0", NULL };
	size_t n;

	make_label(label, sizeof(label), "dump");
	g_cleanup[0] = '\0';
	fp = tmpfile();
	TEST_ASSERT(fp != NULL);
	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, fp) == 0);
	track_plist(path);
	TEST_ASSERT(is_bplist(path));
	rewind(fp);
	n = fread(dump, 1, sizeof(dump) - 1, fp);
	TEST_ASSERT(n > 0);
	TEST_ASSERT(!ferror(fp));
	dump[n] = '\0';
	(void)fclose(fp);
	nl = strchr(dump, '\n');
	TEST_ASSERT(nl != NULL);
	*nl = '\0';
	TEST_ASSERT(strcmp(dump, path) == 0);
	TEST_ASSERT(strstr(nl + 1, "<?xml") != NULL);
	TEST_ASSERT(strstr(nl + 1, label) != NULL);
	TEST_ASSERT(strstr(nl + 1, "/tmp/helloworld") != NULL);
	TEST_ASSERT(strstr(nl + 1, "RunAtLoad") != NULL);
	cleanup_plist();
	return (0);
}

/*
 * print_plist_file reads an on-disk plist once and emits path + XML.
 */
static int
test_print_plist_file(void)
{
	FILE *fp;
	char dump[8192];
	char path[PATH_MAX];
	char label[128];
	char *nl;
	char *argv[] = { "/tmp/helloworld", "0", NULL };
	size_t n;

	make_label(label, sizeof(label), "printfile");
	g_cleanup[0] = '\0';
	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, NULL) == 0);
	track_plist(path);
	fp = tmpfile();
	TEST_ASSERT(fp != NULL);
	TEST_ASSERT(print_plist_file(fp, path) == 0);
	rewind(fp);
	n = fread(dump, 1, sizeof(dump) - 1, fp);
	TEST_ASSERT(n > 0);
	TEST_ASSERT(!ferror(fp));
	dump[n] = '\0';
	(void)fclose(fp);
	nl = strchr(dump, '\n');
	TEST_ASSERT(nl != NULL);
	*nl = '\0';
	TEST_ASSERT(strcmp(dump, path) == 0);
	TEST_ASSERT(strstr(nl + 1, "<?xml") != NULL);
	TEST_ASSERT(strstr(nl + 1, label) != NULL);
	TEST_ASSERT(print_plist_file(NULL, path) == -1);
	TEST_ASSERT(print_plist_file(stdout, NULL) == -1);
	TEST_ASSERT(print_plist_file(stdout, "") == -1);
	TEST_ASSERT(print_plist_file(stdout,
	    "/tmp/xspawn-no-such/no.plist") == -1);
	cleanup_plist();
	return (0);
}

/*
 * plist_copy_label reads Label from the on-disk binary plist.
 */
static int
test_copy_label(void)
{
	char path[PATH_MAX];
	char label[128];
	char got[128];
	char *argv[] = { "/tmp/helloworld", "0", NULL };

	make_label(label, sizeof(label), "copylabel");
	g_cleanup[0] = '\0';
	TEST_ASSERT(write_plist(path, sizeof(path), label, argv, 2,
	    NULL, NULL, 0, NULL) == 0);
	track_plist(path);
	TEST_ASSERT(plist_copy_label(path, got, sizeof(got)) == 0);
	TEST_ASSERT(strcmp(got, label) == 0);
	TEST_ASSERT(plist_copy_label(path, NULL, sizeof(got)) == -1);
	TEST_ASSERT(plist_copy_label(path, got, 0) == -1);
	TEST_ASSERT(plist_copy_label(NULL, got, sizeof(got)) == -1);
	cleanup_plist();
	return (0);
}

int
main(void)
{
	if (test_oneshot() != 0)
		exit(1);
	if (test_keepalive() != 0)
		exit(1);
	if (test_log_paths() != 0)
		exit(1);
	if (test_special_chars() != 0)
		exit(1);
	if (test_cleanup_removes_dir() != 0)
		exit(1);
	if (test_bad_args() != 0)
		exit(1);
	if (test_dump_xml() != 0)
		exit(1);
	if (test_print_plist_file() != 0)
		exit(1);
	if (test_copy_label() != 0)
		exit(1);
	printf("PASS\n");
	return (0);
}
