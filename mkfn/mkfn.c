/*
 * fbpad_mkfn - generate fbpad fonts from truetype fonts
 *
 * Copyright (C) 2009-2016 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the Modified BSD license.
 */
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))
#define LIMIT(n, a, b)	((n) < (a) ? (a) : ((n) > (b) ? (b) : (n)))
#define NGLYPHS		(1 << 14)
#define NFONTS		16		/* number of fonts */
#define DWCHAR		0x40000000u	/* second half of a fullwidth char */
#define STB_TRUETYPE_IMPLEMENTATION

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "chars.h"
#include "isdw.c"
#include "stb_truetype.h"

static stbtt_fontinfo fn[NFONTS];
static float fn_vscale[NFONTS];
static float fn_hscale[NFONTS];
static int fn_size[NFONTS];
static int fn_bold[NFONTS];
static int fn_sr[NFONTS];
static int fn_sc[NFONTS];
static char *fn_buf[NFONTS];
static int fn_cnt;

void mkfn_dim(int *r, int *c)
{
	int x0, y0, x1, y1;
	stbtt_GetFontBoundingBox(&fn[0], &x0, &y0, &x1, &y1);
	*r = (y1 - y0) * fn_vscale[0];
	*c = (x1 - x0) * fn_hscale[0];
}

static int mkfn_base(int n, int rows)
{
	int ascent, descent, linegap;
	stbtt_GetFontVMetrics(&fn[n], &ascent, &descent, &linegap);
	return ascent * fn_vscale[n] + (rows - fn_size[n]) / 2;
}

int mkfn_font(char *path, char *spec)
{
	int n = fn_cnt;
	int fd;
	int hdpi = 100;
	int vdpi = 100;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 1;
	fn_buf[n] = malloc(1 << 20);
	read(fd, fn_buf[n], 1 << 20);
	close(fd);
	if (!stbtt_InitFont(&fn[n], (void *) fn_buf[n],
			stbtt_GetFontOffsetForIndex((void *) fn_buf[n], 0))) {
		free(fn_buf[n]);
		return 1;
	}
	fn_size[n] = 10;
	while (spec && *spec) {
		if (spec[0] == 'b')
			fn_bold[n] = atoi(spec + 1);
		if (spec[0] == 'v')
			vdpi = atoi(spec + 1);
		if (spec[0] == 'h')
			hdpi = atoi(spec + 1);
		if (spec[0] == 'r')
			fn_sr[n] = atoi(spec + 1);
		if (spec[0] == 'c')
			fn_sc[n] = atoi(spec + 1);
		if (spec[0] == 's')
			fn_size[n] = atoi(spec + 1);
		if (isdigit((unsigned char) spec[0]))
			fn_size[n] = atoi(spec);
		spec++;
		while (*spec && strchr("0123456789+-", (unsigned char) *spec))
			spec++;
	}
	fn_hscale[n] = stbtt_ScaleForPixelHeight(&fn[n], fn_size[n]) * hdpi / 100;
	fn_vscale[n] = stbtt_ScaleForPixelHeight(&fn[n], fn_size[n]) * vdpi / 100;
	fn_cnt++;
	return 0;
}

static void mkfn_embolden(unsigned char *bits, int rows, int cols)
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
	int sr, sc;	/* the starting row and column of the bitmap */
	int nr, nc;	/* the number of rows and columns to render */
	int i;
	int g = 0;
	int x0, y0, x1, y1;
	for (i = 0; i < fn_cnt; i++)
		if ((g = stbtt_FindGlyphIndex(&fn[i], c & ~DWCHAR)) > 0)
			break;
	if (i == fn_cnt)
		return 1;
	if (!dst)
		return 0;
	stbtt_GetGlyphBitmapBox(&fn[i], g, fn_hscale[i], fn_vscale[i], &x0, &y0, &x1, &y1);
	sr = LIMIT(mkfn_base(i, rows) + y0 + fn_sr[i], 0, rows);
	sc = LIMIT(x0 + fn_sc[i], 0, cols);
	nr = rows - sr;
	nc = cols - sc;
	memset(bits, 0, rows * cols);
	stbtt_MakeGlyphBitmap(&fn[i], bits + sr * cols + sc, nc, nr, cols,
				fn_hscale[i], fn_vscale[i], g);
	if (fn_bold[i])
		mkfn_embolden(bits, rows, cols);
	return 0;
}

void mkfn_free(void)
{
	int i;
	for (i = 0; i < fn_cnt; i++)
		free(fn_buf[i]);
}

static int rows, cols;

static int fn_glyphs(int *glyphs)
{
	unsigned int i;
	int n = 0, j;
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
	if (fd < 0) fd = 1;
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
	"usage: mkfn [options] font1.ttf spec ... >font.tf\n"
	"\noptions:\n"
	"  -h n  \t\t set glyph height\n"
	"  -w n  \t\t set glyph width\n"
	"  -o n  \t\t output file\n";

int main(int argc, char *argv[])
{
	int i;
	char *wdiff = NULL;
	char *hdiff = NULL;
	char *fout = NULL;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'w')
			wdiff = argv[i][2] ? argv[i] + 2 : argv[++i];
		else if (argv[i][1] == 'h')
			hdiff = argv[i][2] ? argv[i] + 2 : argv[++i];
		else if (argv[i][1] == 'o')
			fout =  argv[i][2] ? argv[i] + 2 : argv[++i];
		else
			fprintf(stderr, usage);
	}
	for (; i < argc; i+=2) {
		char *name = argv[i];
		char *spec = argv[i+1];
		if (!spec || mkfn_font(name, spec)) {
			fprintf(stderr, "mkfn: failed to load or missing spec for <%s>\n", name);
			return 1;
		}
	}
	mkfn_dim(&rows, &cols);
	if (hdiff)
		rows = strchr("-+", hdiff[0]) ? rows + atoi(hdiff) : atoi(hdiff);
	if (wdiff)
		cols = strchr("-+", wdiff[0]) ? cols + atoi(wdiff) : atoi(wdiff);
	output(fout ? open(fout, O_WRONLY | O_CREAT | O_TRUNC, 0600) : 1);
	mkfn_free();
	return 0;
}
