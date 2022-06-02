/* fbpad header file */

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))
#define p(s, ...)\
	{FILE *f = fopen("file", "a");\
	fprintf(f, s, ##__VA_ARGS__);\
	fclose(f);}\

#define ESC		27		/* escape code */
#define NHIST		512		/* scrolling history lines */

/* isdw.c */
#define DWCHAR		0x40000000u	/* 2nd half of a fullwidth char */

int isdw(int c);
int iszw(int c);

/* pad.c */
struct pad_state {
	int fbroff, fbcoff, fbrows, fbcols;
	int rows, cols;
	int fnrows, fncols; /* character height, width */
	int bpp;
	struct font *fonts[3];
	char *gc_mem;		/* cached glyph's memory */
};
extern struct pad_state *pstate;

#define FN_I		0x01000000	/* italic font */
#define FN_B		0x02000000	/* bold font */
#define FN_C		0x00ffffff	/* font color mask */
#define pad_rows() pstate->rows
#define pad_cols() pstate->cols
#define pad_crows() pstate->fnrows
#define pad_ccols() pstate->fncols

void pad_pset(struct pad_state *state);
struct pad_state *pad_init(void);
void pad_free(struct pad_state *state);
void pad_conf(int row, int col, int rows, int cols);
void pad_put(int ch, int r, int c, int fg, int bg);
void pad_fill(int sr, int er, int sc, int ec, int c);
void pad_border(unsigned c, int wid);
char *pad_fbdev(void);

/* term.c */
struct term_state {
	int row, col;
	int fg, bg;
	int mode;
};

struct term {
	int *screen;			/* screen content */
	int *hist;			/* scrolling history */
	int *fgs;			/* foreground color */
	int *bgs;			/* background color */
	int *dirty;			/* changed rows in lazy mode */
	struct pad_state *ps;		/* pad_state for this term */
	struct term_state cur, sav;	/* terminal saved state */
	int fd;				/* terminal file descriptor */
	int hrow;			/* the next history row in hist[] */
	int hpos;			/* scrolling history; position */
	int lazy;			/* lazy mode */
	int pid;			/* pid of the terminal program */
	int top, bot;			/* terminal scrolling region */
	int rows, cols;
};

void term_load(struct term *term, int visible);
void term_save(struct term *term);
void term_read(void);
void term_send(int c);
void spawn(char **args);
void term_exec(char **args);
void term_end(void);
void term_screenshot(void);
void term_scrl(int pos);
void term_redraw(int all);

/* font.c */
struct font *font_open(char *path);
void font_free(struct font *font);
int font_rows(struct font *font);
int font_cols(struct font *font);
int font_bitmap(struct font *font, void *dst, int c);

/* scrsnap.c */
void scr_snap(int idx);
int scr_load(int idx);
void scr_free(int idx);
void scr_done(void);

/* fbpad's framebuffer interface */
#define FBDEV		"/dev/fb0"

/* fb_mode() interpretation */
#define FBM_BPP(m)	(((m) >> 16) & 0x0f)	/* bytes per pixel (4 bits) */
#define FBM_CLR(m)	((m) & 0x0fff)		/* bits per color (12 bits) */
#define FBM_ORD(m)	(((m) >> 20) & 0x07)	/* color order (3 bits) */

/* draw.c */
int fb_init(char *dev);
void fb_free(void);
unsigned fb_mode(void);
char *fb_mem(int r);
int fb_rows(void);
int fb_cols(void);
char *fb_dev(void);
void fb_cmap(void);
unsigned fb_val(int r, int g, int b);
