/*
 * helloworld.c - Bootstrap / CrowdStrike Falcon test program.
 *
 * Prints one line, sleeps, exits 0. Self-contained: no project headers or
 * repo paths. Optional argv[1] = sleep seconds (default 60 for EDR sampling;
 * use 0 for a quick oneshot). Not part of the product client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	unsigned long sleep_secs;
	char *end;

	sleep_secs = 60;
	if (argc >= 2) {
		sleep_secs = strtoul(argv[1], &end, 10);
		if (end == argv[1] || *end != '\0')
			sleep_secs = 60;
		if (sleep_secs > 3600)
			sleep_secs = 3600;
	}

	(void)printf("HELLOWORLD: pid=%d ppid=%d sleep=%lu\n",
	    (int)getpid(), (int)getppid(), sleep_secs);
	(void)fflush(stdout);

	if (sleep_secs > 0)
		(void)sleep((unsigned)sleep_secs);

	return (0);
}
