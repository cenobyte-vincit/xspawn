/*
 * xspawn.c - Bootstrap an ephemeral gui launchd job via XPC.
 *
 * Subcommands oneshot and submit write a temp plist, print its path
 * and an XML copy of the job dictionary to stdout, send the private
 * bootstrap pipe message, then remove the temp directory. load -p
 * bootstraps an existing plist, prints that path and XML, and does
 * not delete the file. remove unloads a job by label. Does not exec
 * launchctl. Same-user unprivileged only; refuses root. Requires C17
 * and macOS.
 */

#include <sys/stat.h>

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <servers/bootstrap.h>

#include "bootstrap-job.h"
#include "label.h"
#include "write-plist.h"

/* libc provides the symbol; headers do not always declare it. */
extern char *__progname;

static void usage(void);
static char *abspath_any(const char *);
static int has_plist_suffix(const char *);
static void cmd_run(int, char **, int);
static void cmd_remove(int, char **);
static void cmd_load(int, char **);
static void atexit_plist(void);
static void refuse_if_loaded(const char *);

static char g_plist_cleanup[PATH_MAX];

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s oneshot -l <label> [-o <stdout>] [-e <stderr>] "
	    "[--] <program> [args...]\n"
	    "       %s submit  -l <label> [-o <stdout>] [-e <stderr>] "
	    "[--] <program> [args...]\n"
	    "       %s remove  -l <label>\n"
	    "       %s load    -p <plist>\n",
	    __progname, __progname, __progname, __progname);
	exit(1);
}

/*
 * Return an absolute copy of path. Absolute paths are duplicated;
 * relative paths are joined to the current working directory.
 * Caller frees the returned string.
 */
static char *
abspath_any(const char *path)
{
	char cwd[PATH_MAX];
	char *out;
	size_t need;

	if (path == NULL)
		errx(1, "empty path");
	if (!*path)
		errx(1, "empty path");
	if (path[0] == '/') {
		out = strdup(path);
		if (out == NULL)
			errx(1, "malloc");
		return (out);
	}
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		err(1, "getcwd");
	need = strlen(cwd) + 1 + strlen(path) + 1;
	out = malloc(need);
	if (out == NULL)
		errx(1, "malloc");
	(void)snprintf(out, need, "%s/%s", cwd, path);
	return (out);
}

/*
 * Return 1 if path ends in ".plist" (lowercase), else 0.
 */
static int
has_plist_suffix(const char *path)
{
	size_t n;

	n = strlen(path);
	if (n < 6)
		return (0);
	return (strcmp(path + n - 6, ".plist") == 0);
}

/*
 * atexit handler: remove the temp plist and its directory.
 */
static void
atexit_plist(void)
{
	write_plist_cleanup(g_plist_cleanup);
}

/*
 * Fail if label is already in gui or user. Runs before write_plist()
 * so a doomed 800 does not write $TMPDIR/XXXXXX/XXXXXX.plist or
 * print the XML copy of the job dictionary. That on-disk file (and
 * the XML copy) is a DFIR artefact (CrowdStrike Falcon ASEPFilePath
 * keeps the path after unlink).
 */
static void
refuse_if_loaded(const char *label)
{
	int loaded;

	loaded = job_is_loaded(bootstrap_port, getuid(), label);
	if (loaded < 0)
		errx(1, "%s", bootstrap_job_errmsg());
	if (loaded != 0)
		errx(1, "label already loaded: %s", label);
}

/*
 * Parse oneshot/submit options, write the temp plist, print path
 * plus XML, then bootstrap. Omitted -o / -e become /dev/null. The
 * temp directory is removed on every exit (0 or non-zero) via atexit.
 */
static void
cmd_run(int argc, char *argv[], int keepalive)
{
	char plist_path[PATH_MAX];
	char *abs_err;
	char *abs_out;
	char **abs_argv;
	char *const *prog_argv;
	const char *label;
	const char *stderr_path;
	const char *stdout_path;
	size_t j;
	size_t prog_argc;
	int i;

	label = NULL;
	stderr_path = NULL;
	stdout_path = NULL;
	abs_err = NULL;
	abs_out = NULL;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
		if (strcmp(argv[i], "-l") == 0) {
			if (i + 1 >= argc)
				usage();
			label = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "-o") == 0) {
			if (i + 1 >= argc)
				usage();
			stdout_path = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "-e") == 0) {
			if (i + 1 >= argc)
				usage();
			stderr_path = argv[++i];
			continue;
		}
		if (argv[i][0] == '-')
			usage();
		break;
	}

	if (label == NULL)
		usage();
	if (i >= argc)
		usage();

	prog_argv = &argv[i];
	prog_argc = (size_t)(argc - i);

	if (!label_is_valid(label))
		errx(1, "invalid label: %s", label);
	if (prog_argv[0][0] != '/')
		errx(1, "program must be an absolute path: %s",
		    prog_argv[0]);
	if (stdout_path == NULL)
		stdout_path = "/dev/null";
	if (stderr_path == NULL)
		stderr_path = "/dev/null";

	refuse_if_loaded(label);
	abs_argv = calloc(prog_argc, sizeof(*abs_argv));
	if (abs_argv == NULL)
		errx(1, "malloc");
	abs_argv[0] = (char *)prog_argv[0];
	for (j = 1; j < prog_argc; j++)
		abs_argv[j] = prog_argv[j];
	abs_out = abspath_any(stdout_path);
	abs_err = abspath_any(stderr_path);

	if (write_plist(plist_path, sizeof(plist_path), label, abs_argv,
	    prog_argc, abs_out, abs_err, keepalive, stdout) != 0)
		err(1, "write plist");
	if (strlcpy(g_plist_cleanup, plist_path,
	    sizeof(g_plist_cleanup)) >= sizeof(g_plist_cleanup)) {
		write_plist_cleanup(plist_path);
		errx(1, "plist path too long");
	}
	if (atexit(atexit_plist) != 0) {
		write_plist_cleanup(plist_path);
		errx(1, "atexit");
	}

	if (bootstrap_job(bootstrap_port, getuid(), plist_path) != 0)
		errx(1, "%s", bootstrap_job_errmsg());

	free(abs_out);
	free(abs_err);
	free(abs_argv);
}

/*
 * Parse remove -l, then bootout the label from the gui domain.
 */
static void
cmd_remove(int argc, char *argv[])
{
	const char *label;
	int i;

	label = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-l") == 0) {
			if (i + 1 >= argc)
				usage();
			label = argv[++i];
			continue;
		}
		usage();
	}
	if (label == NULL)
		usage();
	if (!label_is_valid(label))
		errx(1, "invalid label: %s", label);
	if (bootout_job(bootstrap_port, getuid(), label) != 0)
		errx(1, "%s", bootstrap_job_errmsg());
}

/*
 * Parse load -p, then bootstrap the existing plist. Does not unlink it.
 */
static void
cmd_load(int argc, char *argv[])
{
	struct stat st;
	char label[128];
	const char *path;
	int i;

	path = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0) {
			if (i + 1 >= argc)
				usage();
			path = argv[++i];
			continue;
		}
		usage();
	}
	if (path == NULL)
		usage();
	if (!*path)
		errx(1, "empty path");
	if (path[0] != '/')
		errx(1, "plist must be an absolute path: %s", path);
	if (!has_plist_suffix(path))
		errx(1, "plist path must end in .plist: %s", path);
	if (stat(path, &st) != 0)
		err(1, "%s", path);
	if (!S_ISREG(st.st_mode))
		errx(1, "not a regular file: %s", path);
	if (plist_copy_label(path, label, sizeof(label)) != 0)
		errx(1, "plist missing Label: %s", path);
	if (!label_is_valid(label))
		errx(1, "invalid label: %s", label);
	refuse_if_loaded(label);
	if (print_plist_file(stdout, path) != 0)
		errx(1, "print plist: %s", path);
	if (bootstrap_job(bootstrap_port, getuid(), path) != 0)
		errx(1, "%s", bootstrap_job_errmsg());
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		usage();
	if (geteuid() == 0)
		errx(1, "refusing to run as root");

	if (strcmp(argv[1], "oneshot") == 0)
		cmd_run(argc - 1, argv + 1, 0);
	else if (strcmp(argv[1], "submit") == 0)
		cmd_run(argc - 1, argv + 1, 1);
	else if (strcmp(argv[1], "remove") == 0)
		cmd_remove(argc - 1, argv + 1);
	else if (strcmp(argv[1], "load") == 0)
		cmd_load(argc - 1, argv + 1);
	else
		usage();

	return (0);
}
