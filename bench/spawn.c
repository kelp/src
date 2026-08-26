/*
 * spawn.c - fork + exec + wait latency benchmark.
 *
 * Spawns /usr/bin/true in a loop and measures the full cycle
 * per iteration. Prints percentiles in microseconds.
 */
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_ROUNDS 20000
#define SPAWN_PATH "/usr/bin/true"

static uint64_t
nsnow(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		err(1, "clock_gettime");
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int
cmp(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return (x > y) - (x < y);
}

int
main(int argc, char **argv)
{
	long rounds = (argc > 1) ? atol(argv[1]) : DEFAULT_ROUNDS;
	uint64_t t0, *lat;

	if (rounds < 100)
		rounds = 100;
	lat = calloc(rounds, sizeof(*lat));
	if (!lat)
		err(1, "calloc");

	for (long i = 0; i < rounds; i++) {
		pid_t pid;
		int st;

		t0 = nsnow();
		pid = fork();
		if (pid == -1)
			err(1, "fork");
		if (pid == 0) {
			execl(SPAWN_PATH, "true", (char *)NULL);
			_exit(127);
		}
		if (waitpid(pid, &st, 0) == -1)
			err(1, "waitpid");
		lat[i] = nsnow() - t0;
	}

	qsort(lat, rounds, sizeof(*lat), cmp);
	printf("spawn n=%ld p50_us=%llu p90_us=%llu p99_us=%llu "
	    "p999_us=%llu max_us=%llu\n", rounds,
	    (unsigned long long)(lat[rounds / 2] / 1000),
	    (unsigned long long)(lat[rounds * 9 / 10] / 1000),
	    (unsigned long long)(lat[rounds * 99 / 100] / 1000),
	    (unsigned long long)(lat[rounds * 999 / 1000] / 1000),
	    (unsigned long long)(lat[rounds - 1] / 1000));
	free(lat);
	return (0);
}
