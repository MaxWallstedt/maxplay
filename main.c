#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include "wav.h"

#define BUFFERSIZE 1024

static pa_sample_format_t format_wav_to_pa(struct wav *wav)
{
	switch (wav->format) {
	case WAV_FORMAT_PCM:
		if (wav->bits_per_sample == 8 &&
		    wav->valid_bits_per_sample == 8) {
			return PA_SAMPLE_U8;
		} else if (wav->bits_per_sample == 16 &&
		           wav->valid_bits_per_sample == 16) {
			return PA_SAMPLE_S16LE;
		} else if (wav->bits_per_sample == 32 &&
		           wav->valid_bits_per_sample == 32) {
			return PA_SAMPLE_S32LE;
		} else if (wav->bits_per_sample == 24 &&
		           wav->valid_bits_per_sample == 24) {
			return PA_SAMPLE_S24LE;
		} else if (wav->bits_per_sample == 32 &&
		           wav->valid_bits_per_sample == 24) {
			return PA_SAMPLE_S24_32LE;
		} else {
			return PA_SAMPLE_INVALID;
		}

	case WAV_FORMAT_IEEE_FLOAT:
		if (wav->bits_per_sample != 32 ||
		    wav->valid_bits_per_sample != 32) {
			return PA_SAMPLE_INVALID;
		}

		return PA_SAMPLE_FLOAT32LE;

	case WAV_FORMAT_ALAW:
		if (wav->bits_per_sample != 8 ||
		    wav->valid_bits_per_sample != 8) {
			return PA_SAMPLE_INVALID;
		}

		return PA_SAMPLE_ALAW;

	case WAV_FORMAT_MULAW:
		if (wav->bits_per_sample != 8 ||
		    wav->valid_bits_per_sample != 8) {
			return PA_SAMPLE_INVALID;
		}

		return PA_SAMPLE_ULAW;
	}

	return PA_SAMPLE_INVALID;
}

static size_t downmix(struct wav *wav, uint8_t *buffer)
{
	int64_t samples[6] = {0};
	size_t bytes_per_sample;
	size_t i;

	if (wav->format != WAV_FORMAT_PCM) {
		fprintf(
			stderr,
			"Downmixing error: Unsupported format\n"
		);
		return 0;
	}

	bytes_per_sample = wav->bits_per_sample / 8;

	for (i = 0; i < 6; ++i) {
		if (bytes_per_sample == 2) {
			samples[i] = (int64_t)(((uint64_t)buffer[i * 2]) |
			                       (((uint64_t)buffer[i * 2 + 1]) << 8) |
			                       ((((uint64_t)buffer[i * 2 + 1]) & 0x80) == 0 ? 0 : 0xFFFFFFFFFFFF0000));
		} else if (bytes_per_sample == 3) {
			samples[i] = (int64_t)(((uint64_t)buffer[i * 3]) |
			                       (((uint64_t)buffer[i * 3 + 1]) << 8) |
			                       (((uint64_t)buffer[i * 3 + 2]) << 16) |
			                       ((((uint64_t)buffer[i * 3 + 2]) & 0x80) == 0 ? 0 : 0xFFFFFFFFFF000000));
		} else if (bytes_per_sample == 4) {
			samples[i] = (int64_t)(((uint64_t)buffer[i * 4]) |
			                       (((uint64_t)buffer[i * 4 + 1]) << 8) |
			                       (((uint64_t)buffer[i * 4 + 2]) << 16) |
			                       (((uint64_t)buffer[i * 4 + 3]) << 24) |
			                       ((((uint64_t)buffer[i * 4 + 3]) & 0x80) == 0 ? 0 : 0xFFFFFFFF00000000));
		} else {
			fprintf(
				stderr,
				"Downmixing error: "
				"Unsupported number of bytes per sample\n"
			);
			return 0;
		}
	}

	samples[0] = (uint64_t)(((int64_t)samples[0] +
	                         (int64_t)samples[2] +
	                         (int64_t)samples[3] +
	                         (int64_t)samples[4]) / 4);

	samples[1] = (uint64_t)(((int64_t)samples[1] +
	                         (int64_t)samples[2] +
	                         (int64_t)samples[3] +
	                         (int64_t)samples[5]) / 4);

	for (i = 0; i < 2; ++i) {
		if (bytes_per_sample == 2) {
			buffer[i * 2] = (uint8_t)(((uint64_t)samples[i]) & 0xFF);
			buffer[i * 2 + 1] = (uint8_t)((((uint64_t)samples[i]) >> 8) & 0xFF);
		} else if (bytes_per_sample == 3) {
			buffer[i * 3] = (uint8_t)(((uint64_t)samples[i]) & 0xFF);
			buffer[i * 3 + 1] = (uint8_t)((((uint64_t)samples[i]) >> 8) & 0xFF);
			buffer[i * 3 + 2] = (uint8_t)((((uint64_t)samples[i]) >> 16) & 0xFF);
		} else {
			buffer[i * 4] = (uint8_t)(((uint64_t)samples[i]) & 0xFF);
			buffer[i * 4 + 1] = (uint8_t)((((uint64_t)samples[i]) >> 8) & 0xFF);
			buffer[i * 4 + 2] = (uint8_t)((((uint64_t)samples[i]) >> 16) & 0xFF);
			buffer[i * 4 + 3] = (uint8_t)((((uint64_t)samples[i]) >> 24) & 0xFF);
		}
	}

	return bytes_per_sample * 2;
}

static int main_loop(struct wav *wav, pa_simple *s)
{
	static uint8_t buffer[BUFFERSIZE];
	size_t offset, remaining;
	int more_data = 1;
	int err;

	for (;;) {
		if (!more_data) {
			break;
		}

		offset = 0;
		remaining = BUFFERSIZE;

		while (remaining >= wav->block_align) {
			err = wav_read_sample(wav, &buffer[offset]);

			if (err == -1) {
				wav_perror(wav, "wav_read_sample");
				return -1;
			} else if (err == 0) {
				more_data = 0;
				break;
			}

			/* Downmix 5.1 to stereo */
			if (wav->channels == 6 &&
			    wav->channel_mask == 0x0000060F) {
				size_t inc;
				inc = downmix(wav, &buffer[offset]);

				if (inc == 0) {
					return -1;
				}

				offset += inc;
				remaining -= inc;
			} else {
				offset += wav->block_align;
				remaining -= wav->block_align;
			}
		}

		if (offset == 0) {
			break;
		}

		if (pa_simple_write(s, buffer, offset, &err) < 0) {
			fprintf(
				stderr,
				"pa_simple_write: %s\n",
				pa_strerror(err)
			);
			return -1;
		}
	}

	if (pa_simple_drain(s, &err) < 0) {
		fprintf(stderr, "pa_simple_drain: %s\n", pa_strerror(err));
		return -1;
	}

	return 0;
}

static struct wav *gl_wav;
static pa_simple *gl_s;

static void sigint_handler(int signum)
{
	int err;

	if (signum == SIGINT) {
		if (pa_simple_flush(gl_s, &err) < 0) {
			fprintf(
				stderr,
				"pa_simple_flush: %s\n",
				pa_strerror(err)
			);
		}

		pa_simple_free(gl_s);
		wav_close(gl_wav);
		exit(EXIT_FAILURE);
	}
}

static int register_sigint()
{
	struct sigaction sa;

	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct wav wav;
	pa_simple *s;
	pa_sample_spec ss;
	int err;

	if (argc != 2) {
		fprintf(stderr, "%s file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (wav_open(&wav, argv[1]) == -1) {
		wav_perror(&wav, "wav_open");
		exit(EXIT_FAILURE);
	}

	wav_print(&wav, stdout);
	ss.format = format_wav_to_pa(&wav);

	if (ss.format == PA_SAMPLE_INVALID) {
		fprintf(
			stderr,
			"%s: Invalid PulseAudio format\n",
			wav.filename
		);
		wav_close(&wav);
		exit(EXIT_FAILURE);
	}

	/* Downmix 5.1 to stereo */
	if (wav.channels == 6 && wav.channel_mask == 0x0000060F) {
		ss.channels = 2;
	} else {
		ss.channels = wav.channels;
	}

	ss.rate = wav.samples_per_sec;

	s = pa_simple_new(
		NULL,
		argv[0],
		PA_STREAM_PLAYBACK,
		NULL,
		wav.filename,
		&ss,
		NULL,
		NULL,
		&err
	);

	if (s == NULL) {
		fprintf(stderr, "pa_simple_new: %s\n", pa_strerror(err));
		wav_close(&wav);
		exit(EXIT_FAILURE);
	}

	gl_wav = &wav;
	gl_s = s;

	if (register_sigint() == -1) {
		pa_simple_free(s);
		wav_close(&wav);
		exit(EXIT_FAILURE);
	}

	if (main_loop(&wav, s) == -1) {
		pa_simple_free(s);
		wav_close(&wav);
		exit(EXIT_FAILURE);
	}

	pa_simple_free(s);
	wav_close(&wav);

	return 0;
}
