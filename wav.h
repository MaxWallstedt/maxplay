#ifndef WAV_H
#define WAV_H

#include <stdio.h>
#include <stdint.h>

#define WAV_ERR_OK          0
#define WAV_ERR_FOPEN       1
#define WAV_ERR_FREAD       2
#define WAV_ERR_EOF         3
#define WAV_ERR_MATCH       4
#define WAV_ERR_NOMATCH     5
#define WAV_ERR_FSEEK       6
#define WAV_ERR_UNSUPPORTED 7
#define WAV_ERR_NOFMT       8

enum wav_format {
	WAV_FORMAT_PCM,
	WAV_FORMAT_IEEE_FLOAT,
	WAV_FORMAT_ALAW,
	WAV_FORMAT_MULAW
};

struct wav {
	const char *filename;
	FILE *fptr;
	enum wav_format format;
	uint16_t channels;
	uint32_t samples_per_sec;
	uint16_t block_align;
	uint16_t bits_per_sample;
	uint16_t valid_bits_per_sample;
	uint32_t channel_mask;
	uint32_t length;
	int err;
	int tmp_errno;
};

/**
 * Opens a wav file indicated by 'filename' and stores it in 'wav'.
 * On success, 0 is returned. On error, -1 is returned and 'wav->err'
 * is set to an appropriate error number and 'wav->tmp_errno' is set
 * to the value of 'errno'.
 */
int wav_open(struct wav *wav, const char *filename);

/**
 * Closes the wav file in 'wav->fptr'.
 */
void wav_close(struct wav *wav);

/**
 * Reads one sample block from 'wav->fptr' and writes it to 'dest'.
 * On success, 1 is returned. On EOF, 0 is returned. On error, -1 is
 * returned and 'wav->err' is set to an appropriate error number and
 * 'wav->tmp_errno' is set to the value of 'errno'.
 */
int wav_read_sample(struct wav *wav, void *dest);

/**
 * Prints 'wav' to 'fp'.
 */
void wav_print(struct wav *wav, FILE *fp);

/**
 * Prints the current error indicated by 'wav->err' and
 * 'wav->tmp_errno'. If 'prefix' is not NULL, the error will be
 * prefixed by it.
 */
void wav_perror(struct wav *wav, const char *prefix);

#endif
