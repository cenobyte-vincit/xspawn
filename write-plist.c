/*
 * write-plist.c - Write a temporary binary launchd.plist(5).
 *
 * Creates $TMPDIR/XXXXXX/XXXXXX.plist (or under
 * /tmp) as bplist00 via CoreFoundation, optionally prints the path
 * and XML from the in-memory dict, and removes the file and
 * directory on any write failure.
 */

#include "write-plist.h"

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>

/*
 * Create a UTF-8 CFString. Caller releases. NULL on failure.
 */
static CFStringRef
cfstr(const char *s)
{
	return (CFStringCreateWithCString(kCFAllocatorDefault, s,
	    kCFStringEncodingUTF8));
}

/*
 * Set key to a UTF-8 string value. Returns 0, or -1.
 */
static int
dict_set_string(CFMutableDictionaryRef d, CFStringRef key,
    const char *value)
{
	CFStringRef s;

	s = cfstr(value);
	if (s == NULL)
		return (-1);
	CFDictionarySetValue(d, key, s);
	CFRelease(s);
	return (0);
}

/*
 * Build ProgramArguments from argv. Caller releases. NULL on failure.
 */
static CFMutableArrayRef
argv_array(char *const *argv, size_t argc)
{
	CFMutableArrayRef a;
	CFStringRef s;
	size_t i;

	a = CFArrayCreateMutable(kCFAllocatorDefault, (CFIndex)argc,
	    &kCFTypeArrayCallBacks);
	if (a == NULL)
		return (NULL);
	for (i = 0; i < argc; i++) {
		s = cfstr(argv[i]);
		if (s == NULL) {
			CFRelease(a);
			return (NULL);
		}
		CFArrayAppendValue(a, s);
		CFRelease(s);
	}
	return (a);
}

/*
 * Build the job dictionary. Caller releases. NULL on failure.
 */
static CFMutableDictionaryRef
build_job_dict(const char *label, char *const *argv, size_t argc,
    const char *stdout_path, const char *stderr_path, int keepalive)
{
	CFMutableArrayRef args;
	CFMutableDictionaryRef d;

	d = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
	    &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	if (d == NULL)
		return (NULL);
	if (dict_set_string(d, CFSTR("Label"), label) != 0)
		goto cleanup;
	args = argv_array(argv, argc);
	if (args == NULL)
		goto cleanup;
	CFDictionarySetValue(d, CFSTR("ProgramArguments"), args);
	CFRelease(args);
	if (keepalive)
		CFDictionarySetValue(d, CFSTR("KeepAlive"),
		    kCFBooleanTrue);
	else {
		CFDictionarySetValue(d, CFSTR("RunAtLoad"),
		    kCFBooleanTrue);
		CFDictionarySetValue(d, CFSTR("LaunchOnlyOnce"),
		    kCFBooleanTrue);
	}
	if (stdout_path != NULL &&
	    dict_set_string(d, CFSTR("StandardOutPath"),
	    stdout_path) != 0)
		goto cleanup;
	if (stderr_path != NULL &&
	    dict_set_string(d, CFSTR("StandardErrorPath"),
	    stderr_path) != 0)
		goto cleanup;
	return (d);
cleanup:
	CFRelease(d);
	return (NULL);
}

/*
 * Write all n bytes of buf to fd. Returns 0, or -1.
 */
static int
write_all(int fd, const void *buf, size_t n)
{
	const unsigned char *p;
	size_t off;
	ssize_t w;

	p = buf;
	off = 0;
	while (off < n) {
		w = write(fd, p + off, n - off);
		if (w <= 0)
			return (-1);
		off += (size_t)w;
	}
	return (0);
}

/*
 * Write path and an XML serialisation of plist to fp.
 * XML comes from the in-memory object; no extra open.
 */
static int
print_plist_xml(FILE *fp, const char *path, CFPropertyListRef plist)
{
	CFDataRef data;
	const unsigned char *p;
	CFIndex n;

	if (fprintf(fp, "%s\n", path) < 0)
		return (-1);
	data = CFPropertyListCreateData(kCFAllocatorDefault, plist,
	    kCFPropertyListXMLFormat_v1_0, 0, NULL);
	if (data == NULL) {
		errno = EIO;
		return (-1);
	}
	p = CFDataGetBytePtr(data);
	n = CFDataGetLength(data);
	if (n > 0 && fwrite(p, 1, (size_t)n, fp) != (size_t)n) {
		CFRelease(data);
		return (-1);
	}
	if (n == 0 || p[n - 1] != '\n') {
		if (fputc('\n', fp) == EOF) {
			CFRelease(data);
			return (-1);
		}
	}
	CFRelease(data);
	if (fflush(fp) != 0)
		return (-1);
	return (0);
}

/*
 * Serialise dict as bplist00 onto fd. Returns 0, or -1.
 */
static int
write_bplist(int fd, CFDictionaryRef d)
{
	CFDataRef data;
	int rc;

	data = CFPropertyListCreateData(kCFAllocatorDefault, d,
	    kCFPropertyListBinaryFormat_v1_0, 0, NULL);
	if (data == NULL)
		return (-1);
	rc = write_all(fd, CFDataGetBytePtr(data),
	    (size_t)CFDataGetLength(data));
	CFRelease(data);
	return (rc);
}

/*
 * Copy the parent directory of path into dir. Returns 0, or -1.
 */
static int
parent_dir(char *dir, size_t dir_len, const char *path)
{
	char *slash;

	if (path == NULL || *path == '\0')
		return (-1);
	if (strlcpy(dir, path, dir_len) >= dir_len)
		return (-1);
	slash = strrchr(dir, '/');
	if (slash == NULL || slash == dir)
		return (-1);
	*slash = '\0';
	return (0);
}

/*
 * Unlink plist_path and rmdir its parent. NULL or empty is a no-op.
 */
void
write_plist_cleanup(const char *plist_path)
{
	char dir[PATH_MAX];

	if (plist_path == NULL || *plist_path == '\0')
		return;
	(void)unlink(plist_path);
	if (parent_dir(dir, sizeof(dir), plist_path) != 0)
		return;
	(void)rmdir(dir);
}

/*
 * Close fd (if >= 0) and remove the plist plus its directory.
 * Always returns -1.
 */
static int
fail_cleanup(int fd, const char *path)
{
	if (fd >= 0)
		(void)close(fd);
	write_plist_cleanup(path);
	return (-1);
}

/*
 * Return $TMPDIR if it is an absolute path, otherwise /tmp.
 * The CLI refuses root, so getenv is sufficient.
 */
static const char *
tmp_base(void)
{
	const char *dir;

	dir = getenv("TMPDIR");
	if (dir == NULL || dir[0] != '/')
		return ("/tmp");
	return (dir);
}

/*
 * Create $TMPDIR/XXXXXX (0700) into dir.
 */
static int
make_temp_dir(char *dir, size_t dir_len)
{
	const char *base;
	size_t n;
	int written;

	base = tmp_base();
	n = strlen(base);
	while (n > 1 && base[n - 1] == '/')
		n--;
	written = snprintf(dir, dir_len,
	    "%.*s/XXXXXX", (int)n, base);
	if (written < 0 || (size_t)written >= dir_len)
		return (-1);
	if (mkdtemp(dir) == NULL)
		return (-1);
	return (0);
}

/*
 * Write XXXXXX.plist in a new temp directory and store that path.
 */
int
write_plist(char *path_buf, size_t path_len, const char *label,
    char *const *argv, size_t argc, const char *stdout_path,
    const char *stderr_path, int keepalive, FILE *dump)
{
	CFMutableDictionaryRef d;
	char dir[PATH_MAX];
	size_t i;
	int fd;
	int n;

	if (path_buf == NULL || path_len == 0)
		return (-1);
	if (label == NULL || argv == NULL || argc == 0)
		return (-1);
	for (i = 0; i < argc; i++) {
		if (argv[i] == NULL)
			return (-1);
	}

	if (make_temp_dir(dir, sizeof(dir)) != 0)
		return (-1);
	n = snprintf(path_buf, path_len, "%s/XXXXXX.plist", dir);
	if (n < 0 || (size_t)n >= path_len) {
		(void)rmdir(dir);
		return (-1);
	}
	fd = mkstemps(path_buf, 6);
	if (fd < 0) {
		(void)rmdir(dir);
		return (-1);
	}
	d = build_job_dict(label, argv, argc, stdout_path, stderr_path,
	    keepalive);
	if (d == NULL)
		return (fail_cleanup(fd, path_buf));
	if (dump != NULL && print_plist_xml(dump, path_buf, d) != 0) {
		CFRelease(d);
		return (fail_cleanup(fd, path_buf));
	}
	if (write_bplist(fd, d) != 0) {
		CFRelease(d);
		return (fail_cleanup(fd, path_buf));
	}
	CFRelease(d);
	if (close(fd) != 0)
		return (fail_cleanup(-1, path_buf));
	return (0);
}

/*
 * Read path once and parse it. Caller CFReleases. NULL on failure.
 */
static CFPropertyListRef
read_plist(const char *path)
{
	struct stat st;
	CFDataRef data;
	CFPropertyListRef plist;
	unsigned char *buf;
	size_t n;
	size_t off;
	ssize_t r;
	int fd;

	if (path == NULL || *path == '\0')
		return (NULL);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (NULL);
	if (fstat(fd, &st) != 0 || st.st_size <= 0) {
		(void)close(fd);
		return (NULL);
	}
	n = (size_t)st.st_size;
	buf = malloc(n);
	if (buf == NULL)
		errx(1, "malloc");
	off = 0;
	while (off < n) {
		r = read(fd, buf + off, n - off);
		if (r <= 0) {
			free(buf);
			(void)close(fd);
			return (NULL);
		}
		off += (size_t)r;
	}
	(void)close(fd);
	data = CFDataCreate(kCFAllocatorDefault, buf, (CFIndex)n);
	free(buf);
	if (data == NULL)
		return (NULL);
	plist = CFPropertyListCreateWithData(kCFAllocatorDefault, data,
	    kCFPropertyListImmutable, NULL, NULL);
	CFRelease(data);
	return (plist);
}

/*
 * Read path once, parse the property list, print path + XML.
 */
int
print_plist_file(FILE *fp, const char *path)
{
	CFPropertyListRef plist;
	int rc;

	if (fp == NULL)
		return (-1);
	plist = read_plist(path);
	if (plist == NULL)
		return (-1);
	rc = print_plist_xml(fp, path, plist);
	CFRelease(plist);
	return (rc);
}

/*
 * Copy Label from an on-disk plist into buf.
 */
int
plist_copy_label(const char *path, char *buf, size_t buflen)
{
	CFPropertyListRef plist;
	CFStringRef label;
	int ok;

	if (buf == NULL || buflen == 0)
		return (-1);
	plist = read_plist(path);
	if (plist == NULL)
		return (-1);
	if (CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
		CFRelease(plist);
		return (-1);
	}
	label = CFDictionaryGetValue(plist, CFSTR("Label"));
	if (label == NULL || CFGetTypeID(label) != CFStringGetTypeID()) {
		CFRelease(plist);
		return (-1);
	}
	ok = CFStringGetCString(label, buf, (CFIndex)buflen,
	    kCFStringEncodingUTF8);
	CFRelease(plist);
	if (!ok || buf[0] == '\0')
		return (-1);
	return (0);
}
