/*
 * tone.c - write a sine wave WAV to a file, for the audio
 * glitch benchmark. Mono or stereo, 44100 Hz, s16le.
 */
#include <err.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
put32(FILE *fp, uint32_t v)
{
	unsigned char b[4] = { v, v >> 8, v >> 16, v >> 24 };

	fwrite(b, 1, 4, fp);
}

static void
put16(FILE *fp, uint16_t v)
{
	unsigned char b[2] = { v, v >> 8 };

	fwrite(b, 1, 2, fp);
}

int
main(int argc, char **argv)
{
	double seconds = (argc > 2) ? atof(argv[2]) : 60.0;
	int freq = (argc > 3) ? atoi(argv[3]) : 440;
	uint32_t datalen = seconds * 44100 * 2 * sizeof(short);
	FILE *fp;

	if (argc < 2) {
		fprintf(stderr, "usage: tone outfile [seconds] [freq]\n");
		return (1);
	}
	fp = fopen(argv[1], "w");
	if (!fp)
		err(1, "fopen %s", argv[1]);

	fwrite("RIFF", 1, 4, fp);
	put32(fp, 36 + datalen);
	fwrite("WAVEfmt ", 1, 8, fp);
	put32(fp, 16);
	put16(fp, 1);
	put16(fp, 2);
	put32(fp, 44100);
	put32(fp, 44100 * 4);
	put16(fp, 4);
	put16(fp, 16);
	fwrite("data", 1, 4, fp);
	put32(fp, datalen);

	for (uint32_t i = 0; i < datalen / 4; i++) {
		double s = sin(2.0 * M_PI * freq * i / 44100.0) * 20000.0;
		short v = s;

		put16(fp, (uint16_t)v);
		put16(fp, (uint16_t)v);
	}
	fclose(fp);
	return (0);
}
