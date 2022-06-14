/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Amol Surati */

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/* Note that the resolution is aligned to 32x64 pixels */
#define WIDTH			1280ul
#define HEIGHT			768ul

#define NUM_UTILES		(WIDTH * HEIGHT / (8 * 8))
#define NUM_UTILES_PER_MTILE	(4 * 8)
#define NUM_MTILES		(NUM_UTILES / NUM_UTILES_PER_MTILE)

#define NUM_WORDS_PER_UTILE	(8 * 8)

static const int utile_decoder[8][8] = {
	{0, 1, 2, 3, 8, 9, 10, 11},
	{4, 5, 6, 7, 12, 13, 14, 15},

	{16, 17, 18, 19, 24, 25, 26, 27},
	{20, 21, 22, 23, 28, 29, 30, 31},

	{32, 33, 34, 35, 40, 41, 42, 43},
	{36, 37, 38, 39, 44, 45, 46, 47},

	{48, 49, 50, 51, 56, 57, 58, 59},
	{52, 53, 54, 55, 60, 61, 62, 63},
};

static const int mtile_decoder_0x[8][4] = {
	{0,1,2,3},	/*0*/
	{4,5,6,7},	/*1*/
	{8,9,10,11},	/*2*/
	{12,13,14,15},	/*3*/

	{17,16,19,18},	/*4*/
	{21,20,23,22},	/*5*/
	{25,24,27,26},	/*6*/
	{29,28,31,30},	/*7*/
};

static const int mtile_decoder_0[4][8] = {
	{0,4,2,6,1,5,3,7},
	{1,5,3,7,0,4,2,6},
	{2,6,0,4,3,7,1,5},
	{3,7,1,5,2,6,0,4},
};

/* Switch pairs from _0x */
static const int mtile_decoder_1x[8][4] = {
	{2,3,0,1},	/*0*/
	{6,7,4,5},	/*1*/
	{10,11,8,9},	/*2*/
	{14,15,12,13},	/*3*/

	{19,18,17,16},	/*4*/
	{23,22,21,20},	/*5*/
	{27,26,25,24},	/*6*/
	{31,30,29,28},	/*7*/
};

static const int mtile_decoder_1[4][8] = {
	{1,5,3,7,0,4,2,6},
	{0,4,2,6,1,5,3,7},
	{3,7,1,5,2,6,0,4},
	{2,6,0,4,3,7,1,5},
};

/* UTile has size 8 * 8 * 4 = 256 bytes */
static
void decode_utile(const uint32_t *buf, uint32_t *out)
{
	int row, col, ix;

	for (row = 0; row < 8; ++row) {
		for (col = 0; col < 8; ++col) {
			ix = utile_decoder[row][col];
			out[row * 8 + col] = buf[ix];
		}
	}
}

static
void decode_mtile(const uint32_t *buf, int row, int col, uint32_t *out)
{
	int ur, uc, i, num, pixrow, j, r, odd, pixcol;
	static uint32_t utile[64];
	uint32_t *p;

	odd = (row % 2);

	num = NUM_WORDS_PER_UTILE;
	for (i = 0; i < NUM_UTILES_PER_MTILE; ++i) {
		/* The utils are sequentially found, but later rearranged. */
		decode_utile(&buf[i * num], utile);

		/*
		 * utile is in the rasterized format: 8x8 pixels. Place it
		 * within the output mtile appropriately.
		 */

		/* Now calc. the final location of the current utile */
		for (ur = 0; ur < 8; ++ur) {
			for (uc = 0; uc < 4; ++uc) {
				if (!odd && mtile_decoder_0x[ur][uc] == i)
					goto done;
				if (odd && mtile_decoder_1x[ur][uc] == i)
					goto done;
			}
		}
done:
		assert(ur < 8 && uc < 4);
		for (j = 0; j < 8; ++j) {
			if (!odd && mtile_decoder_0[col % 4][j] == ur)
				break;
			if (odd && mtile_decoder_1[col % 4][j] == ur)
				break;
		}
		ur = j;
		assert(ur < 8 && uc < 4);

		/* ur and uc denotes the final location of the utile i */
		pixrow = ur * 8 * WIDTH + uc * 8;

		/* pixrow/col are the top-left corner of the utile. */
		for (r = 0; r < 8; ++r, pixrow += WIDTH)
			memcpy(&out[pixrow], &utile[r * 8], 32);
	}
}

int main(int argc, char **argv)
{
	int size, row, col, pixrow, pixcol;
	FILE *f;
	uint32_t *buf, *out, *mtile;

	if (argc != 3) {
		printf("Usage: %s cb.bin out.bin\n", argv[0]);
		return EINVAL;
	}

	f = fopen(argv[1], "rb");
	if (f == NULL)
		return errno;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	buf = malloc(size);
	if (buf == NULL)
		return ENOMEM;
	fread(buf, 1, size, f);
	fclose(f);

	assert(size == WIDTH * HEIGHT * 4);
	out = malloc(size);
	if (out == NULL)
		return ENOMEM;
	memset(out, 0, size);

	/* out is 1280x768 words */
	for (row = 0; row < HEIGHT / 64; ++row) {
		for (col = 0; col < WIDTH / 32; ++col) {
			/* top left corner of the mtile in the output */
			pixrow = row * 64;
			pixcol = col * 32;
			mtile = &out[pixrow * WIDTH + pixcol];

			decode_mtile(buf, row, col, mtile);
			buf += NUM_UTILES_PER_MTILE * NUM_WORDS_PER_UTILE;
		}
	}

	f = fopen(argv[2], "wb");
	if (f == NULL)
		return errno;
	fwrite(out, 1, size, f);
	fclose(f);
}
