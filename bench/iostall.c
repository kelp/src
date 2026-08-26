/*
 * iostall.c - bulk write vs small-read stall benchmark.
 *
 * One thread streams zeros into a large file. A second thread
 * probes every 2ms with open + pread + close on a small file
 * and records each operation's latency. Prints probe latency
 * percentiles, which expose stalls caused by writeback bursts.
 */
#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *dir;
static double seconds;
static uint64_t *lat;
static volatile int done;
static long nprobe;

#define BULK_BS (128 * 1024)
#define PROBE_SZ 4096

static uint64_t
nsnow(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		err(1, "clock_gettime");
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void *
writer(void *arg)
{
	char path[4096], buf[BULK_BS];
	uint64_t deadline = nsnow() + seconds * 1e9;
	ssize_t n;
	int fd;

	(void)arg;
	memset(buf, 0, sizeof(buf));
	snprintf(path, sizeof(path), "%s/bulk.bin", dir);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1)
		err(1, "open %s", path);
	while (nsnow() < deadline) {
		n = write(fd, buf, sizeof(buf));
		if (n == -1)
			err(1, "write");
	}
	close(fd);
	done = 1;
	return (NULL);
}

static void *
prober(void *arg)
{
	char path[4096];
	char buf[PROBE_SZ];
	uint64_t t0;
	ssize_t n;
	int fd;

	(void)arg;
	snprintf(path, sizeof(path), "%s/probe.bin", dir);
	while (!done) {
		t0 = nsnow();
		fd = open(path, O_RDONLY);
		if (fd == -1)
			err(1, "open %s", path);
		n = pread(fd, buf, sizeof(buf), PROBE_SZ * (nprobe % 8));
		if (n != sizeof(buf))
			err(1, "pread");
		close(fd);
		lat[nprobe++] = nsnow() - t0;
		usleep(2000);
	}
	return (NULL);
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
	pthread_t tw, tp;
	char path[4096], buf[64 * 1024];
	long cap;

	if (argc < 3) {
		fprintf(stderr, "usage: iostall workdir seconds\n");
		return (1);
	}
	dir = argv[1];
	seconds = atof(argv[2]);
	cap = seconds * 1000 + 16;
	lat = calloc(cap, sizeof(*lat));
	if (!lat)
		err(1, "calloc");

	snprintf(path, sizeof(path), "%s/probe.bin", dir);
	memset(buf, 'x', sizeof(buf));
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1)
		err(1, "open %s", path);
	for (int i = 0; i < 16; i++) {
		ssize_t n = pwrite(fd, buf, sizeof(buf),
		    i * sizeof(buf));

		if (n != sizeof(buf))
			err(1, "pwrite");
	}
	close(fd);

	if (pthread_create(&tw, NULL, writer, NULL) ||
	    pthread_create(&tp, NULL, prober, NULL))
		err(1, "pthread_create");
	pthread_join(tw, NULL);
	pthread_join(tp, NULL);

	qsort(lat, nprobe, sizeof(*lat), cmp);
	printf("iostall probes=%ld p50_us=%llu p99_us=%llu max_us=%llu\n",
	    nprobe,
	    (unsigned long long)(lat[nprobe / 2] / 1000),
	    (unsigned long long)(lat[nprobe * 99 / 100] / 1000),
	    (unsigned long long)(lat[nprobe - 1] / 1000));
	unlink(path);
	snprintf(path, sizeof(path), "%s/bulk.bin", dir);
	unlink(path);
	free(lat);
	return (0);
}
