/*
 * wakeup.c - pipe ping-pong wakeup latency benchmark.
 *
 * Two threads alternate strictly over two pipes. The responder
 * records the time from the requester's write() to its own
 * poll(2) wake plus read. Prints percentiles in microseconds.
 */
#include <err.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_ROUNDS 200000

static int req[2], rsp[2];
static uint64_t *lat;
static long rounds;

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

static void *
responder(void *arg)
{
	struct pollfd pf;
	uint64_t t, sent;

	(void)arg;
	for (long i = 0; i < rounds; i++) {
		pf.fd = req[0];
		pf.events = POLLIN;
		if (poll(&pf, 1, -1) != 1)
			err(1, "poll");
		t = nsnow();
		if (read(req[0], &sent, sizeof(sent)) != sizeof(sent))
			err(1, "read");
		lat[i] = t - sent;
		if (write(rsp[1], &sent, sizeof(sent)) != sizeof(sent))
			err(1, "write");
	}
	return (NULL);
}

int
main(int argc, char **argv)
{
	pthread_t th;
	uint64_t t;

	rounds = (argc > 1) ? atol(argv[1]) : DEFAULT_ROUNDS;
	if (rounds < 100)
		rounds = 100;
	lat = calloc(rounds, sizeof(*lat));
	if (!lat)
		err(1, "calloc");

	if (pipe(req) || pipe(rsp))
		err(1, "pipe");
	if (pthread_create(&th, NULL, responder, NULL))
		err(1, "pthread_create");

	for (long i = 0; i < rounds; i++) {
		t = nsnow();
		if (write(req[1], &t, sizeof(t)) != sizeof(t))
			err(1, "write");
		if (read(rsp[0], &t, sizeof(t)) != sizeof(t))
			err(1, "read");
	}
	pthread_join(th, NULL);

	qsort(lat, rounds, sizeof(*lat), cmp);
	printf("wakeup n=%ld p50_ns=%llu p90_ns=%llu p99_ns=%llu "
	    "p999_ns=%llu max_ns=%llu\n", rounds,
	    (unsigned long long)lat[rounds / 2],
	    (unsigned long long)lat[rounds * 9 / 10],
	    (unsigned long long)lat[rounds * 99 / 100],
	    (unsigned long long)lat[rounds * 999 / 1000],
	    (unsigned long long)lat[rounds - 1]);
	free(lat);
	return (0);
}
