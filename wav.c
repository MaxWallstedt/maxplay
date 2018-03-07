#include "wav.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

static int wav_seek_n(struct wav *wav, size_t n)
{
	if (fseek(wav->fptr, (long)n, SEEK_CUR) == -1) {
		wav->err = WAV_ERR_FSEEK;
		wav->tmp_errno = errno;
		return -1;
	}

	return 0;
}

static int wav_read_n(struct wav *wav, size_t n, void *dest)
{
	size_t nread;

	nread = fread(dest, n, 1, wav->fptr);

	if (nread == 0) {
		if (feof(wav->fptr)) {
			wav->err = WAV_ERR_EOF;
			return -1;
		} else {
			wav->err = WAV_ERR_FREAD;
			wav->tmp_errno = errno;
			return -1;
		}
	}

	return 0;
}

static int wav_read_u16(struct wav *wav, void *dest)
{
	uint16_t buf;
	void *ptr;
	size_t nread;

	if (dest == NULL) {
		ptr = &buf;
	} else {
		ptr = dest;
	}

	nread = fread(ptr, 2, 1, wav->fptr);

	if (nread == 0) {
		if (feof(wav->fptr)) {
			wav->err = WAV_ERR_EOF;
			return -1;
		} else {
			wav->err = WAV_ERR_FREAD;
			wav->tmp_errno = errno;
			return -1;
		}
	}

	return 0;
}

static int wav_read_u32(struct wav *wav, void *dest)
{
	uint32_t buf;
	void *ptr;
	size_t nread;

	if (dest == NULL) {
		ptr = &buf;
	} else {
		ptr = dest;
	}

	nread = fread(ptr, 4, 1, wav->fptr);

	if (nread == 0) {
		if (feof(wav->fptr)) {
			wav->err = WAV_ERR_EOF;
			return -1;
		} else {
			wav->err = WAV_ERR_FREAD;
			wav->tmp_errno = errno;
			return -1;
		}
	}

	return 0;
}

static int wav_read_str4(struct wav *wav, const char *match, void *dest)
{
	char buf[4];
	char *ptr;

	if (dest == NULL) {
		ptr = buf;
	} else {
		ptr = dest;
	}

	if (match != NULL && strlen(match) != 4) {
		wav->err = WAV_ERR_MATCH;
		return -1;
	}

	if (wav_read_u32(wav, ptr) == -1) {
		return -1;
	}

	if (match != NULL && strncmp(match, (const char *)ptr, 4) != 0) {
		wav->err = WAV_ERR_NOMATCH;
		return -1;
	}

	return 0;
}

static int wav_read_fmt_chunk(struct wav *wav)
{
	uint32_t cksize;
	uint16_t format_tag;
	uint32_t avg_bytes_per_sec;
	uint32_t cbsize;
	uint8_t sub_format[16] = {0};

	/* cksize */
	if (wav_read_u32(wav, &cksize) == -1) {
		return -1;
	}

	/* wFormatTag */
	if (wav_read_u16(wav, &format_tag) == -1) {
		return -1;
	}

	/* nChannels */
	if (wav_read_u16(wav, &wav->channels) == -1) {
		return -1;
	}

	/* nSamplesPerSec */
	if (wav_read_u32(wav, &wav->samples_per_sec) == -1) {
		return -1;
	}

	/* nAvgBytesPerSec */
	if (wav_read_u32(wav, &avg_bytes_per_sec) == -1) {
		return -1;
	}

	/* nBlockAlign */
	if (wav_read_u16(wav, &wav->block_align) == -1) {
		return -1;
	}

	/* wBitsPerSample */
	if (wav_read_u16(wav, &wav->bits_per_sample) == -1) {
		return -1;
	}

	wav->valid_bits_per_sample = wav->bits_per_sample;

	if (cksize > 16) {
		/* cbSize */
		if (wav_read_u16(wav, &cbsize) == -1) {
			return -1;
		}

		if (cbsize > 0) {
			/* wValidBitsPerSample */
			if (wav_read_u16(wav,
			                 &wav->valid_bits_per_sample)
			    == -1) {
				return -1;
			}

			/* dwChannelMask */
			if (wav_read_u32(wav, &wav->channel_mask) == -1) {
				return -1;
			}

			/* SubFormat */
			if (wav_read_n(wav, 16, sub_format) == -1) {
				return -1;
			}

			/* Verify the last 14 bytes of SubFormat. */
			if (strncmp("\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", (const char *)&sub_format[2], 14) != 0) {
				wav->err = WAV_ERR_UNSUPPORTED;
				return -1;
			}
		}
	}

	/* Set format */
	switch (format_tag) {
	case 0x0001:
		wav->format = WAV_FORMAT_PCM;
		break;

	case 0x0003:
		wav->format = WAV_FORMAT_IEEE_FLOAT;
		break;

	case 0x0006:
		wav->format = WAV_FORMAT_ALAW;
		break;

	case 0x0007:
		wav->format = WAV_FORMAT_MULAW;
		break;

	case 0xFFFE:
		/* The format tag for WAVE_FORMAT_EXTENSIBLE is in
		   the first two bytes of SubFormat. */
		switch (*(uint16_t *)sub_format) {
		case 0x0001:
			wav->format = WAV_FORMAT_PCM;
			break;

		case 0x0003:
			wav->format = WAV_FORMAT_IEEE_FLOAT;
			break;

		case 0x0006:
			wav->format = WAV_FORMAT_ALAW;
			break;

		case 0x0007:
			wav->format = WAV_FORMAT_MULAW;
			break;

		default:
			wav->err = WAV_ERR_UNSUPPORTED;
			return -1;
		}

		break;

	default:
		wav->err = WAV_ERR_UNSUPPORTED;
		return -1;
	}

	/* Verify nAvgBytesPerSec, since it is not stored in the data
	   structure. */
	if (avg_bytes_per_sec != wav->block_align * wav->samples_per_sec) {
		wav->err = WAV_ERR_UNSUPPORTED;
		return -1;
	}

	return 0;
}

static int wav_read_unknown_chunk(struct wav *wav)
{
	uint32_t cksize;

	/* cksize */
	if (wav_read_u32(wav, &cksize) == -1) {
		return -1;
	}

	if (wav_seek_n(wav, cksize) == -1) {
		return -1;
	}

	return 0;
}

/**
 * Reads the initial format data from 'wav->fptr' and stores it in
 * 'wav'.
 * On success, 0 is returned. On error, -1 is returned and 'wav->err'
 * is set to an appropriate error number and 'wav->tmp_errno' is set
 * to the value of 'errno'.
 */
static int wav_read_init(struct wav *wav)
{
	char ck_id[4];
	int has_fmt = 0;

	/**
	 * Read master RIFF chunk.
	 */

	/* ckID */
	if (wav_read_str4(wav, "RIFF", NULL) == -1) {
		return -1;
	}

	/* cksize */
	if (wav_read_u32(wav, NULL) == -1) {
		return -1;
	}

	/* WAVEID */
	if (wav_read_str4(wav, "WAVE", NULL) == -1) {
		return -1;
	}

	/**
	 * Read remaining chunks until a data chunk is reached.
	 */

	for (;;) {
		/* ckID */
		if (wav_read_str4(wav, NULL, ck_id) == -1) {
			return -1;
		}

		if (strncmp("data", ck_id, 4) == 0) {
			/* cksize */
			if (wav_read_u32(wav, &wav->length) == -1) {
				return -1;
			}

			break;
		} else if (strncmp("fmt ", ck_id, 4) == 0) {
			/* Read fmt chunk. */
			if (wav_read_fmt_chunk(wav) == -1) {
				return -1;
			}

			has_fmt = 1;
		} else {
			if (wav_read_unknown_chunk(wav) == -1) {
				return -1;
			}
		}
	}

	if (!has_fmt) {
		wav->err = WAV_ERR_NOFMT;
		return -1;
	}

	return 0;
}

/**
 * Opens a wav file indicated by 'filename' and stores it in 'wav'.
 * On success, 0 is returned. On error, -1 is returned and 'wav->err'
 * is set to an appropriate error number and 'wav->tmp_errno' is set
 * to the value of 'errno'.
 */
int wav_open(struct wav *wav, const char *filename)
{
	wav->err = WAV_ERR_OK;
	wav->tmp_errno = 0;
	wav->filename = filename;
	wav->fptr = fopen(filename, "r");

	if (wav->fptr == NULL) {
		wav->err = WAV_ERR_FOPEN;
		wav->tmp_errno = errno;
		return -1;
	}

	wav->channel_mask = 0x80000000;

	if (wav_read_init(wav) == -1) {
		fclose(wav->fptr);
		wav->fptr = NULL;
		return -1;
	}

	if (wav->channel_mask == 0x80000000) {
		if (wav->channels == 1) {
			wav->channel_mask = 0x00000004;
		} else if (wav->channels == 2) {
			wav->channel_mask = 0x00000003;
		}
	}

	return 0;
}

/**
 * Closes the wav file in 'wav->fptr'.
 */
void wav_close(struct wav *wav)
{
	fclose(wav->fptr);
}

/**
 * Reads one sample block from 'wav->fptr' and writes it to 'dest'.
 * On success, 1 is returned. On EOF, 0 is returned. On error, -1 is
 * returned and 'wav->err' is set to an appropriate error number and
 * 'wav->tmp_errno' is set to the value of 'errno'.
 */
int wav_read_sample(struct wav *wav, void *dest)
{
	size_t nread;

	nread = fread(dest, wav->block_align, 1, wav->fptr);

	if (nread == 0) {
		if (feof(wav->fptr)) {
			return 0;
		} else {
			wav->err = WAV_ERR_FREAD;
			wav->tmp_errno = errno;
			return -1;
		}
	}

	return 1;
}

static const char *wav_sp_pos[18] = {
	"FRONT_LEFT",
	"FRONT_RIGHT",
	"FRONT_CENTER",
	"LOW_FREQUENCY",
	"BACK_LEFT",
	"BACK_RIGHT",
	"FRONT_LEFT_OF_CENTER",
	"FRONT_RIGHT_OF_CENTER",
	"BACK_CENTER",
	"SIDE_LEFT",
	"SIDE_RIGHT",
	"TOP_CENTER",
	"TOP_FRONT_LEFT",
	"TOP_FRONT_CENTER",
	"TOP_FRONT_RIGHT",
	"TOP_BACK_LEFT",
	"TOP_BACK_CENTER",
	"TOP_BACK_RIGHT"
};

/**
 * Prints 'wav' to 'fp'.
 */
void wav_print(struct wav *wav, FILE *fp)
{
	fprintf(fp, "%s {\n", wav->filename);
	fprintf(fp, "\tformat                = ");

	switch (wav->format) {
	case WAV_FORMAT_PCM:
		fprintf(fp, "WAV_FORMAT_PCM,\n");
		break;

	case WAV_FORMAT_IEEE_FLOAT:
		fprintf(fp, "WAV_FORMAT_IEEE_FLOAT,\n");
		break;

	case WAV_FORMAT_ALAW:
		fprintf(fp, "WAV_FORMAT_ALAW,\n");
		break;

	case WAV_FORMAT_MULAW:
		fprintf(fp, "WAV_FORMAT_MULAW,\n");
		break;
	}

	fprintf(fp, "\tchannels              = %u\n", wav->channels);
	fprintf(fp, "\tsamples_per_sec       = %u\n", wav->samples_per_sec);
	fprintf(fp, "\tblock_align           = %u\n", wav->block_align);
	fprintf(fp, "\tbits_per_sample       = %u\n", wav->bits_per_sample);
	fprintf(fp, "\tvalid_bits_per_sample = %u\n", wav->valid_bits_per_sample);
	fprintf(fp, "\terr                   = %d\n", wav->err);
	fprintf(fp, "\ttmp_errno             = %d\n", wav->tmp_errno);
	fprintf(fp, "\tchannel_mask          =\n");

	{
		int i, j = 0;

		for (i = 0; i < 18; ++i) {
			if (((wav->channel_mask >> i) & 1) == 1) {
				fprintf(
					stderr,
					"\t\t%2d: %s",
					j,
					wav_sp_pos[i]
				);
				++j;

				if (j < wav->channels) {
					fprintf(stderr, ",\n");
				} else {
					fprintf(stderr, "\n");
				}
			}
		}
	}

	{
		uint32_t hours;
		uint32_t minutes;
		uint32_t seconds;

		seconds = wav->length /
		          (wav->bits_per_sample / 8 *
		           wav->samples_per_sec) / wav->channels;
		minutes = seconds / 60;
		seconds -= minutes * 60;
		hours = minutes / 60;
		minutes -= hours * 60;

		fprintf(
			fp,
			"\tlength                = %02u:%02u:%02u\n",
			hours,
			minutes,
			seconds
		);
	}

	fprintf(fp, "}\n");
}


/**
 * Prints the current error indicated by 'wav->err' and
 * 'wav->tmp_errno'. If 'prefix' is not NULL, the error will be
 * prefixed by it.
 */
void wav_perror(struct wav *wav, const char *prefix)
{
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}

	switch (wav->err) {
	case WAV_ERR_OK:
		fprintf(stderr, "No error\n");
		break;

	case WAV_ERR_FOPEN:
		fprintf(
			stderr,
			"%s: fopen: %s\n",
			wav->filename,
			strerror(wav->tmp_errno)
		);
		break;

	case WAV_ERR_FREAD:
		fprintf(
			stderr,
			"%s: fread: %s\n",
			wav->filename,
			strerror(wav->tmp_errno)
		);
		break;

	case WAV_ERR_EOF:
		fprintf(
			stderr,
			"%s: Unexpected EOF\n",
			wav->filename
		);
		break;

	case WAV_ERR_MATCH:
		fprintf(
			stderr,
			"wav_read_str4: Invalid match string entered\n"
		);
		break;

	case WAV_ERR_NOMATCH:
		fprintf(
			stderr,
			"wav_read_str4: "
			"Read data does not match input string\n"
		);
		break;

	case WAV_ERR_FSEEK:
		fprintf(
			stderr,
			"%s: fseek: %s\n",
			wav->filename,
			strerror(wav->tmp_errno)
		);
		break;

	case WAV_ERR_UNSUPPORTED:
		fprintf(
			stderr,
			"%s: Unsupported format\n",
			wav->filename
		);
		break;

	case WAV_ERR_NOFMT:
		fprintf(
			stderr,
			"%s: No fmt chunk found\n",
			wav->filename
		);
		break;
	}
}
