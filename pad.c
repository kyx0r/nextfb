struct pad_state *pstate;

/* glyph bitmap cache: use CGLCNT lists of size CGLLEN each */
#define GCLCNT		(1 << 7)		/* glyph cache list count */
#define GCLLEN		(1 << 4)		/* glyph cache list length */
#define GCGLEN		(pstate->fnrows * pstate->fncols * 4)	/* bytes to store a glyph */
#define GCN		(GCLCNT * GCLLEN)	/* total glpyhs */
#define GCIDX(c)	((c) & (GCLCNT - 1))

static int gc_next[GCLCNT];	/* the next slot to use in each list */
static int gc_glyph[GCN];	/* cached glyphs */
static int gc_bg[GCN];
static int gc_fg[GCN];
static char gc_bits[1024 * 4];
static char gc_row[32 * 1024];

void pad_pset(struct pad_state *state)
{
	memset(gc_row, 0, sizeof(gc_row));
	memset(gc_bits, 0, sizeof(gc_bits));
	memset(gc_fg, 0, sizeof(gc_fg[0]) * GCN);
	memset(gc_bg, 0, sizeof(gc_bg[0]) * GCN);
	memset(gc_next, 0, sizeof(gc_next[0]) * GCLCNT);
	memset(gc_glyph, 0, sizeof(gc_glyph[0]) * GCN);
	pstate = state;
}

static int pad_font(char *fr, char *fi, char *fb)
{
	struct font *r = fr ? font_open(fr) : NULL;
	if (!r)
		return 1;
	pstate->fonts[0] = r;
	pstate->fonts[1] = fi ? font_open(fi) : NULL;
	pstate->fonts[2] = fb ? font_open(fb) : NULL;
	pstate->fnrows = font_rows(pstate->fonts[0]);
	pstate->fncols = font_cols(pstate->fonts[0]);
	return 0;
}

struct pad_state *pad_init(void)
{
	pstate = malloc(sizeof(struct pad_state));
	if (!pstate || pad_font(FR, FI, FB))
		return NULL;
	if (!(pstate->gc_mem = malloc(GCLCNT * GCLLEN * GCGLEN)))
		return NULL;
	pstate->rows = fb_rows() / pstate->fnrows;
	pstate->cols = fb_cols() / pstate->fncols;
	pstate->bpp = FBM_BPP(fb_mode());
	pad_conf(0, 0, fb_rows(), fb_cols());
	return pstate;
}

void pad_conf(int roff, int coff, int _rows, int _cols)
{
	pstate->fbroff = roff;
	pstate->fbcoff = coff;
	pstate->fbrows = _rows;
	pstate->fbcols = _cols;
	pstate->rows = pstate->fbrows / pstate->fnrows;
	pstate->cols = pstate->fbcols / pstate->fncols;
}

void pad_free(struct pad_state *state)
{
	free(state->gc_mem);
	for (int i = 0; i < 3; i++)
		if (state->fonts[i])
			font_free(state->fonts[i]);
	free(state);
}

#define CR(a)		(((a) >> 16) & 0x0000ff)
#define CG(a)		(((a) >> 8) & 0x0000ff)
#define CB(a)		((a) & 0x0000ff)
#define COLORMERGE(f, b, c)		((b) + (((f) - (b)) * (c) >> 8u))

static unsigned mixed_color(int fg, int bg, unsigned val)
{
	unsigned char r = COLORMERGE(CR(fg), CR(bg), val);
	unsigned char g = COLORMERGE(CG(fg), CG(bg), val);
	unsigned char b = COLORMERGE(CB(fg), CB(bg), val);
	return FB_VAL(r, g, b);
}

static unsigned color2fb(int c)
{
	return FB_VAL(CR(c), CG(c), CB(c));
}

static char *gc_get(int c, int fg, int bg)
{
	int idx = GCIDX(c) * GCLLEN;
	int i;
	for (i = idx; i < idx + GCLLEN; i++)
		if (gc_glyph[i] == c && gc_fg[i] == fg && gc_bg[i] == bg)
			return pstate->gc_mem + i * GCGLEN;
	return NULL;
}

static char *gc_put(int c, int fg, int bg)
{
	int lst = GCIDX(c);
	int pos = gc_next[lst]++;
	int idx = lst * GCLLEN + pos;
	if (gc_next[lst] >= GCLLEN)
		gc_next[lst] = 0;
	gc_glyph[idx] = c;
	gc_fg[idx] = fg;
	gc_bg[idx] = bg;
	return pstate->gc_mem + idx * GCGLEN;
}

static void bmp2fb(char *d, char *s, int fg, int bg, int nr, int nc)
{
	int i, j, k;
	for (i = 0; i < pstate->fnrows; i++) {
		char *p = d + i * pstate->fncols * pstate->bpp;
		for (j = 0; j < pstate->fncols; j++) {
			unsigned v = i < nr && j < nc ?
				(unsigned char) s[i * nc + j] : 0;
			unsigned c = mixed_color(fg, bg, v);
			for (k = 0; k < pstate->bpp; k++)	/* little-endian */
				*p++ = (c >> (k * 8)) & 0xff;
		}
	}
}

static char *ch2fb(int fn, int c, int fg, int bg)
{
	char *fbbits;
	if (c < 0 || (c < 128 && (!isprint(c) || isspace(c))))
		return NULL;
	if ((fbbits = gc_get(c, fg, bg)))
		return fbbits;
	if (font_bitmap(pstate->fonts[fn], gc_bits, c))
		return NULL;
	fbbits = gc_put(c, fg, bg);
	bmp2fb(fbbits, gc_bits, fg & FN_C, bg & FN_C,
		font_rows(pstate->fonts[fn]), font_cols(pstate->fonts[fn]));
	return fbbits;
}

static void fb_set(int r, int c, void *mem, int len)
{
	memcpy(fb_mem(pstate->fbroff + r) + (pstate->fbcoff + c) * pstate->bpp, mem, len * pstate->bpp);
}

static void fb_box(int sr, int er, int sc, int ec, unsigned val)
{
	int i, k;
	char *p = gc_row;
	for (i = 0; i < ec - sc; i++)
		for (k = 0; k < pstate->bpp; k++)	/* little-endian */
			*p++ = (val >> (k * 8)) & 0xff;
	for (i = sr; i < er; i++)
		fb_set(i, sc, gc_row, ec - sc);
}

void pad_border(unsigned c, int wid)
{
	int v = color2fb(c & FN_C);
	if (pstate->fbroff < wid || pstate->fbcoff < wid)
		return;
	fb_box(-wid, 0, -wid, pstate->fbcols + wid, v);
	fb_box(pstate->fbrows, pstate->fbrows + wid, -wid, pstate->fbcols + wid, v);
	fb_box(-wid, pstate->fbrows + wid, -wid, 0, v);
	fb_box(-wid, pstate->fbrows + wid, pstate->fbcols, pstate->fbcols + wid, v);
}

static int fnsel(int fg, int bg)
{
	if ((fg | bg) & FN_B)
		return pstate->fonts[2] ? 2 : 0;
	if ((fg | bg) & FN_I)
		return pstate->fonts[1] ? 1 : 0;
	return 0;
}

void pad_put(int ch, int r, int c, int fg, int bg)
{
	int sr = pstate->fnrows * r;
	int sc = pstate->fncols * c;
	char *bits;
	int i;
	bits = ch2fb(fnsel(fg, bg), ch, fg, bg);
	if (!bits)
		bits = ch2fb(0, ch, fg, bg);
	if (!bits)
		fb_box(sr, sr + pstate->fnrows, sc, sc + pstate->fncols, color2fb(bg & FN_C));
	else
		for (i = 0; i < pstate->fnrows; i++)
			fb_set(sr + i, sc, bits + (i * pstate->fncols * pstate->bpp), pstate->fncols);
}

void pad_fill(int sr, int er, int sc, int ec, int c)
{
	int fber = er >= 0 ? er * pstate->fnrows : pstate->fbrows;
	int fbec = ec >= 0 ? ec * pstate->fncols : pstate->fbcols;
	fb_box(sr * pstate->fnrows, fber, sc * pstate->fncols, fbec, color2fb(c & FN_C));
}

char *pad_fbdev(void)
{
	static char fbdev[2048];
	snprintf(fbdev, sizeof(fbdev), "FBDEV=%s:%dx%d%+d%+d",
		fb_dev(), pstate->fbcols, pstate->fbrows, pstate->fbcoff, pstate->fbroff);
	return fbdev;
}
