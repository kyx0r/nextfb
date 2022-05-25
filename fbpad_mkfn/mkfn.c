/*
 * fbpad_mkfn - generate fbpad fonts from truetype fonts
 *
 * Copyright (C) 2009-2022 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "chars.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define LIMIT(n, a, b)	((n) < (a) ? (a) : ((n) > (b) ? (b) : (n)))
#define NFONTS		16		/* number of fonts */
#define DWCHAR		0x40000000u	/* second half of a fullwidth char */
#define NGLYPHS		(1 << 14)

static FT_Library library;
static FT_Face fn[NFONTS];
static int fn_desc[NFONTS];
static int fn_bold[NFONTS];
static int fn_hint[NFONTS];
static int fn_sc[NFONTS];
static int fn_sr[NFONTS];
static int fn_cnt;

static int glyphload(FT_Face face, int c, int autohint)
{
	int flags = FT_LOAD_RENDER;
	if (autohint)
		flags |= FT_LOAD_FORCE_AUTOHINT;
	return !FT_Get_Char_Index(face, c) ||
			FT_Load_Char(face, c, flags);
}

static int facedescender(FT_Face face)
{
	char *s = "gy_pj/\\|Q";
	int ret = 0;
	for (; *s; s++)
		if (!glyphload(face, *s, 0))
			ret = MAX(ret, face->glyph->bitmap.rows -
					face->glyph->bitmap_top);
	return ret;
}

int mkfn_font(char *path, char *spec)
{
	FT_Face *face = &fn[fn_cnt];
	int hdpi = 196;
	int vdpi = 196;
	float size = 10;
	if (FT_New_Face(library, path, 0, face))
		return 1;
	while (spec && *spec) {
		if (spec[0] == 'a')
			fn_hint[fn_cnt] = atoi(spec + 1);
		if (spec[0] == 'b')
			fn_bold[fn_cnt] = atoi(spec + 1);
		if (spec[0] == 'v')
			vdpi = atoi(spec + 1);
		if (spec[0] == 'c')
			fn_sc[fn_cnt] = atof(spec + 1);
		if (spec[0] == 'r')
			fn_sr[fn_cnt] = atof(spec + 1);
		if (spec[0] == 'h')
			hdpi = atoi(spec + 1);
		if (spec[0] == 's')
			size = atof(spec + 1);
		if (isdigit((unsigned char) spec[0]))
			size = atof(spec);
		spec++;
		while (*spec && strchr("0123456789.-+", (unsigned char) *spec))
			spec++;
	}
	if (FT_Set_Char_Size(*face, 0, size * 64, hdpi, vdpi))
		return 1;
	fn_desc[fn_cnt] = facedescender(*face);
	fn_cnt++;
	return 0;
}

static void fn_embolden(unsigned char *bits, int rows, int cols)
{
	int i, j, k;
	int n = 2;
	for (i = 0; i < rows; i++) {
		for (j = cols - n; j >= 0; j--) {
			int idx = i * cols + j;
			int val = 0;
			for (k = 0; k < n; k++)
				if (bits[idx + k] > val)
					val = bits[idx + k];
			bits[idx + n - 1] = val;
		}
	}
}

int mkfn_bitmap(char *dst, int c, int rows, int cols)
{
	unsigned char *bits = (void *) dst;
	int sr, sc;	/* the starting and the number of rows in bits */
	int nr, nc;	/* the starting and the number of cols in bits */
	int br;		/* the starting column of glyph bitmap */
	int bc;		/* the starting row of glyph bitmap */
	int bw;		/* the number of columns in glyph bitmap */
	int i, j;
	int bold;
	unsigned char *src;
	FT_Face face = NULL;
	for (i = 0; i < fn_cnt; i++) {
		if (!glyphload(fn[i], c & ~DWCHAR, fn_hint[i])) {
			face = fn[i];
			bold = fn_bold[i];
			break;
		}
	}
	if (!face)
		return 1;
	if (!dst)
		return 0;
	bc = c & DWCHAR ? cols : 0;
	nc = LIMIT(face->glyph->bitmap.width - bc, 0, cols);
	sc = LIMIT(face->glyph->bitmap_left - bc + fn_sc[i], 0, cols - nc);
	sr = rows + fn_sr[i] - fn_desc[i] - face->glyph->bitmap_top;
	br = MAX(0, -sr);
	sr = LIMIT(sr, 0, rows);
	nr = MIN(rows - sr, face->glyph->bitmap.rows - br);
	memset(bits, 0, rows * cols);
	src = face->glyph->bitmap.buffer;
	bw = face->glyph->bitmap.pitch;
	if (face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
		for (i = 0; i < nr; i++) {
			for (j = 0; j < nc; j++) {
				int num = (br + i) * bw * 8 + bc + j;
				int val = src[num / 8] & (1 << (7 - num % 8));
				bits[(i + sr) * cols + j + sc] = val ? 255 : 0;
			}
		}
		return 0;
	}
	for (i = 0; i < nr; i++)
		memcpy(&bits[(sr + i) * cols + sc], src + (br + i) * bw + bc, nc);
	if (bold)
		fn_embolden(bits, rows, cols);
	return 0;
}

void mkfn_dim(int *r, int *c)
{
	*r = fn[0]->size->metrics.height >> 6;
	*c = fn[0]->size->metrics.max_advance >> 6;
}

void mkfn_init(void)
{
	FT_Init_FreeType(&library);
}

void mkfn_free(void)
{
	int i;
	for (i = 0; i < fn_cnt; i++)
		FT_Done_Face(fn[i]);
	FT_Done_FreeType(library);
}

static int rows, cols;

static int fn_glyphs(int *glyphs)
{
	int i, j;
	int n = 0;
	for (i = 0; i < sizeof(chars) / sizeof(chars[0]); i++) {
		for (j = chars[i][0]; j <= chars[i][1] && n < NGLYPHS; j++) {
			if (!mkfn_bitmap(NULL, j, rows, cols))
				glyphs[n++] = j;
			if (!mkfn_bitmap(NULL, j, rows, cols) && isdw(j))
				glyphs[n++] = DWCHAR | j;
		}
	}
	return n;
}

static int intcmp(const void *v1, const void *v2)
{
	return *(int *) v1 - *(int *) v2;
}

/*
 * This tinyfont header is followed by:
 *
 * glyphs[n]	unicode character codes (int)
 * bitmaps[n]	character bitmaps (char[rows * cols])
 */
struct tinyfont {
	char sig[8];	/* tinyfont signature; "tinyfont" */
	int ver;	/* version; 0 */
	int n;		/* number of glyphs */
	int rows, cols;	/* glyph dimensions */
};

/* generate the output tinyfont font */
static void output(int fd)
{
	char *sig = "tinyfont";
	struct tinyfont head;
	int glyphs[NGLYPHS];
	char *buf = malloc(rows * cols);
	int i;
	memcpy(head.sig, sig, strlen(sig));
	head.ver = 0;
	head.rows = rows;
	head.cols = cols;
	head.n = fn_glyphs(glyphs);
	qsort(glyphs, head.n, sizeof(glyphs[0]), intcmp);
	write(fd, &head, sizeof(head));
	write(fd, glyphs, sizeof(*glyphs) * head.n);
	for (i = 0; i < head.n; i++) {
		mkfn_bitmap(buf, glyphs[i], rows, cols);
		write(fd, buf, rows * cols);
	}
	fprintf(stderr, "tinyfont[%5d]: height=%2d width=%2d\n",
		head.n, rows, cols);
	free(buf);
}

static char *usage =
	"usage: mkfn [options] font1.ttf:size ... >font.tf\n"
	"\noptions:\n"
	"  -h n  \t\t set glyph height\n"
	"  -w n  \t\t set glyph width\n";

int main(int argc, char *argv[])
{
	int i;
	char *wdiff = NULL;
	char *hdiff = NULL;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'w') {
			wdiff = argv[i][2] ? argv[i] + 2 : argv[++i];
		} else if (argv[i][1] == 'h') {
			hdiff = argv[i][2] ? argv[i] + 2 : argv[++i];
		} else {
			i = argc;
		}
	}
	if (i == argc) {
		fprintf(stderr, usage);
		return 0;
	}
	mkfn_init();
	for (; i < argc; i++) {
		char *name = argv[i];
		char *spec = NULL;
		if (strchr(name, ':')) {
			spec = strrchr(name, ':') + 1;
			strrchr(name, ':')[0] = '\0';
		}
		if (mkfn_font(name, spec)) {
			fprintf(stderr, "mkfn: failed to load <%s>\n", name);
			return 1;
		}
	}
	mkfn_dim(&rows, &cols);
	if (hdiff)
		rows = strchr("-+", hdiff[0]) ? rows + atoi(hdiff) : atoi(hdiff);
	if (wdiff)
		cols = strchr("-+", wdiff[0]) ? cols + atoi(wdiff) : atoi(wdiff);
	output(1);
	mkfn_free();
	return 0;
}
