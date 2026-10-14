/*
 * NEXTFB FRAMEBUFFER VIRTUAL TERMINAL
 *
 * Copyright (C) 2009-2021 Ali Gholami Rudi <ali at rudi dot ir>
 * Copyright (C) 2020-2021 Kyryl Melekhin <k dot melekhin at gmail dot com>
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
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <linux/input.h>
#include <unistd.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include "fb.h"
#include "conf.h"

#include "font.c"
#include "term.c"
#include "pad.c"
#include "draw.c"
#include "mkfn/isdw.c"
#include "scrsnap.c"

#define CTRLKEY(x)	((x) - 96)
#define POLLFLAGS	(POLLIN | POLLHUP | POLLERR | POLLNVAL)
#define NTAGS		(int)(sizeof(tags) - 1)
#define NTERMS		(NTAGS * 2)
#define TERMSNAP(i)	(strchr(TAGS_SAVED, tags[(i) % NTAGS]))

static char tags[] = TAGS;
static struct term *terms[NTERMS];
static struct pad_state *ipstate;	/* initial pad state */
static int tops[NTAGS];			/* top terms of tags */
static int split[NTAGS];		/* terms are shown together */
static int ctag;			/* current tag */
static int ltag;			/* the last tag */
static int exitit;
static int hidden;			/* do not touch the framebuffer */
static int locked;
static int taglock;			/* disable tag switching */
static char pass[1024];
static unsigned int passlen;
static int cmdmode;			/* execute a command and exit */
static int tagslisted;

/* signals only set these flags and poke the pipe, the main loop does the
   work: hiding and redrawing terminals is not async-signal-safe, and a
   handler can land in the middle of the very drawing it would redo */
static int sigpipe[2] = {-1, -1};
static volatile sig_atomic_t sig_rel, sig_acq, sig_child, sig_quit;


/* ---- mouse support ---- */

#define DOUBLE_MS	400	/* same-cell clicks closer than this merge */
#define CLICK_REG_MS	1000	/* a lone click registers only after this */
#define MOUSE_DIR	"/dev/input"
#define WHEEL_STEP	3

static int mouse_bit(int fd, int type, int bit);
static int mouse_try(char *path);

static int mfd = -1;		/* current mouse input event device */
static int mx, my;		/* software mouse cursor position (pixels) */
static int mbprev;		/* previously pressed mouse buttons */
static long long mtime;		/* time of the last button press (ms) */
static int mlast_r, mlast_c;	/* cell of the previous click */
static int mpress_r, mpress_c;	/* cell the left button went down in */
static int mdrag;		/* the held left button left that cell */
static int msel_mode;		/* selection anchor: 0 cell 1 word 2 line */
static int mclick;		/* consecutive click count on same cell */
static int sel_on;		/* a mouse selection is active */
static int mark_on;		/* a left click has set a marker */
static int mpend;		/* a left click awaits CLICK_REG_MS to mark */
static int mark_r, mark_c;	/* marker cell */
static int mmode;		/* 0 selection, 1 send mouse events */
static int abs_min[2], abs_max[2];	/* absolute axis range, if reported */
static char *yank_buf;	/* yank register; kept until the next selection */
static int yank_all;	/* the register is selected text: paste every line of it */
static int sel_br, sel_bc;	/* selection begin cell */
static int sel_er, sel_ec;	/* selection end cell */

static long long mnow(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		return 0;
	return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int mcell_r(void)
{
	return LIMIT((my - pstate->fbroff) / pad_crows(), 0, pad_rows() - 1);
}

static int mcell_c(void)
{
	return LIMIT((mx - pstate->fbcoff) / pad_ccols(), 0, pad_cols() - 1);
}

static int mword(int c)
{
	return c > ' ' && !(c & DWCHAR) ? 1 : 0;
}

/* assemble the text between two visible cells into a malloc'd string */
static char *mtext(int br, int bc, int er, int ec)
{

	if (br > er || (br == er && bc > ec)) {
		int t;
		t = br; br = er; er = t;
		t = bc; bc = ec; ec = t;
	}
	/* a selection made before a resize can reach past the current grid;
	   clamp it so the cell count cannot outrun the buffer sized below */
	br = LIMIT(br, 0, pad_rows() - 1);
	er = LIMIT(er, 0, pad_rows() - 1);
	bc = LIMIT(bc, 0, pad_cols() - 1);
	ec = LIMIT(ec, 0, pad_cols() - 1);
	int r, c, n = pad_cols() * 5;
	char *buf = malloc((er - br + 1) * n + 1);
	char *s = buf;
	if (!buf)
		return NULL;

	for (r = br; r <= er; r++) {
		int cb = r == br ? bc : 0;
		int ce = r == er ? ec : pad_cols() - 1;
		for (c = cb; c <= ce; c++) {
			int ch = term_getchar(r, c);
			if (~ch & DWCHAR)
				s += writeutf8(s, ch);
		}
		if (r != er)
			*s++ = '\n';
	}
	*s = '\0';
	return buf;
}

static void msel_word(int r, int c)
{
	int l, rt;
	if (!mword(term_getchar(r, c))) {
		sel_br = sel_er = r;
		sel_bc = sel_ec = c;
		sel_on = 1;
		return;
	}
	for (l = c; l > 0 && mword(term_getchar(r, l - 1)); l--)
		;
	for (rt = c; rt < pad_cols() - 1 && mword(term_getchar(r, rt + 1)); rt++)
		;
	sel_br = sel_er = r;
	sel_bc = l;
	sel_ec = rt;
	sel_on = 1;
}

static void msel_line(int r)
{
	sel_br = sel_er = r;
	sel_bc = 0;
	sel_ec = pad_cols() - 1;
	sel_on = 1;
}

/* middle button: insert the yank register; it outlives the selection
   and any keystroke, and is only replaced by a new selection */
static void mpaste(void)
{
	char *s;
	for (s = yank_buf; s && *s && (yank_all || *s != 0x0a); s++)
		term_send(*s);
}

static void mclick_ev(int r, int c)
{
	long long now = mnow();
	if (r == mlast_r && c == mlast_c && now - mtime < DOUBLE_MS)
		mclick++;
	else
		mclick = 1;
	mtime = now;
	mlast_r = mpress_r = r;
	mlast_c = mpress_c = c;
	mdrag = 0;
	msel_mode = mclick >= 3 ? 2 : mclick == 2 ? 1 : 0;
	if (mclick >= 3) {
		msel_line(r);
		mark_on = mpend = 0;
	} else if (mclick == 2) {
		msel_word(r, c);
		mark_on = mpend = 0;
	} else if (mark_on) {
		sel_br = mark_r;
		sel_bc = mark_c;
		sel_er = r;
		sel_ec = c;
		sel_on = 1;
		msel_mode = 0;
		mark_on = 0;
		mclick = 0;	/* the region is complete: the next click is a
				   fresh first click that re-anchors it */
	} else {
		/* the first click only parks the pending marker; it becomes a
		   real marker in mouse_tick() once CLICK_REG ms go by with no
		   further click, so double and triple clicks stay clean */
		mpend = 1;
		mark_r = r;
		mark_c = c;
		mtime = now;
		sel_br = sel_er = r;
		sel_bc = sel_ec = c;
		sel_on = 0;
	}
}

/* what lands in the register is echoed back into the terminal when it is
   pasted, so keep only text that could be typed: no control bytes (they
   would inject escape sequences) and no fullwidth cell halves */
static void yank_sanitize(char *s)
{
	char *r = s, *w = s;
	while (*r) {
		unsigned char c = *r++;
		if (c == '\n' || c == '\t' || c >= 0x80 || isprint(c))
			*w++ = c;
	}
	*w = '\0';
}

/* take ownership of s as the new yank register; an empty s after
   sanitizing leaves the previous register alone */
static void yank_set(char *s, int all)
{
	if (!s)
		return;
	yank_sanitize(s);
	if (!*s) {
		free(s);
		return;
	}
	free(yank_buf);
	yank_buf = s;
	yank_all = all;
}

/* put the selection text in the yank register immediately; drop trailing
   blanks since the selection usually reaches the end of a line */
static void msel_yank(void)
{
	char *s;
	int e;
	if (!sel_on)
		return;
	s = mtext(sel_br, sel_bc, sel_er, sel_ec);
	if (!s)
		return;
	e = (int)strlen(s);
	while (e && (s[e - 1] == ' ' || s[e - 1] == '\n'))
		s[--e] = '\0';
	yank_set(s, 1);
}

/* hand the current cursor cell and selection to the term layer; the
   yank register is refreshed only when the selection itself changed */
static void mouse_refresh(int yank)
{
	int br = sel_br, bc = sel_bc, er = sel_er, ec = sel_ec, t;
	if (sel_on && (br > er || (br == er && bc > ec))) {
		t = br; br = er; er = t;
		t = bc; bc = ec; ec = t;
	}
	if ((mark_on || mpend) && !sel_on) {
		/* the block cursor is the pointer and must keep following it;
		   a marker is only an anchor, so it takes a cell of its own in
		   the region slots instead of holding the cursor still on
		   itself, which is what froze the pointer display */
		br = er = mark_r;
		bc = ec = mark_c;
		term_mouse(mcell_r(), mcell_c(), 1, br, bc, er, ec);
	} else {
		term_mouse(mcell_r(), mcell_c(), sel_on, br, bc, er, ec);
	}
	if (yank)
		msel_yank();
}

static void mclear(void)
{
	if (mrow < 0 && !sel_on && !mark_on && !mpend)
		return;
	mark_on = 0;
	mpend = 0;
	mclick = 0;
	mdrag = 0;
	msel_mode = 0;
	sel_on = 0;
	term_mouse(-1, 0, 0, 0, 0, 0, 0);
}

static void msendmouse(int button, int r, int c, int press)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
		button, c + 1, r + 1, press ? 'M' : 'm');
	term_sendstr(buf);
}

/* a lone left click turns into a marker only after CLICK_REG_MS passes
   with no second click; pollterms() runs this every poll cycle */
static void mouse_tick(void)
{
	if (!mpend || mmode || mnow() - mtime < CLICK_REG_MS)
		return;
	mpend = 0;
	mark_on = 1;
	mclick = 1;
	msel_mode = 0;
	mouse_refresh(0);
}

static void mouse_event(int type, int code, int value)
{
	int r = mcell_r(), c = mcell_c();
	int orow = mrow, ocol = mcol, dirty = 0, selup = 0, motion = 0;
	switch (type) {
	case EV_REL:
		if (code == REL_X || code == REL_Y) {
			dirty = motion = 1;
			if (code == REL_X)
				mx = LIMIT(mx + value, 0, fb_cols() - 1);
			else
				my = LIMIT(my + value, 0, fb_rows() - 1);
		} else if (code == REL_WHEEL || code == REL_WHEEL_HI_RES ||
			(code == REL_HWHEEL && !mouse_bit(mfd, EV_REL, REL_WHEEL))) {
			if (mmode) {
				if (value) {
					int button = value > 0 ? 64 : 65;
					msendmouse(button, r, c, 1);
					msendmouse(button, r, c, 0);
				}
			} else {
				term_scrl(value > 0 ? WHEEL_STEP : -WHEEL_STEP);
			}
		}
		break;
	case EV_ABS:
		dirty = motion = 1;
		if (code == ABS_X || code == ABS_Y) {
			int i = code == ABS_Y, w = (i ? fb_rows() : fb_cols()) - 1;
			if (abs_max[i] > abs_min[i])
				value = (int) ((long long) (value - abs_min[i]) * w /
					(abs_max[i] - abs_min[i]));
			if (i)
				my = LIMIT(value, 0, w);
			else
				mx = LIMIT(value, 0, w);
		}
		break;
	case EV_KEY:
		if (code == BTN_LEFT) {
			if (value && !(mbprev & 1)) {
				if (mmode) {
					msendmouse(0, r, c, 1);
					msendmouse(0, r, c, 0);
				} else {
					mclick_ev(r, c);
				}
			}
			mbprev = value ? mbprev | 1 : mbprev & ~1;
			dirty = selup = 1;
		} else if (code == BTN_RIGHT) {
			if (value && !(mbprev & 2)) {
				if (mmode) {
					mmode = 0;	/* a right click leaves send mode */
				} else if (sel_on || mark_on || mpend) {
					mbprev = mbprev | 2;
					mclear();
					return;
				} else {
					mmode = 1;
					mpend = 0;
					mclick = 0;
				}
			}
			mbprev = value ? mbprev | 2 : mbprev & ~2;
			dirty = 0;
			selup = 0;
		} else if (code == BTN_MIDDLE) {
			if (value && !(mbprev & 4)) {
				if (mmode) {
					msendmouse(2, r, c, 1);
					msendmouse(2, r, c, 0);
				} else {
					mpaste();
				}
			}
			mbprev = value ? mbprev | 4 : mbprev & ~4;
			dirty = 1;
		}
		break;
	}
	if (type == EV_KEY && value)
		dirty = 2;	/* a click repaints at once, no motion needed */

	/* r and c above were read before this event moved the pointer: a
	   motion event judged on the cell it is leaving looks like no change
	   and is dropped, so take the cell the pointer is in now */
	if (motion) {
		r = mcell_r();
		c = mcell_c();
	}

	/* a drag is the left button still held down, the release event is
	   what clears that bit, that has left the cell the press landed in;
	   movement still inside that cell is jitter and must leave the
	   pending click for mouse_tick() to promote. mdrag latches it so
	   tracking continues when the pointer comes back over the press
	   cell, and cells inside a word or line picked by double or triple
	   click must not shrink */
	if ((mbprev & 1) && motion && !mmode &&
		(mdrag || r != mpress_r || c != mpress_c)) {
		int sr = sel_br, sc = sel_bc, er2 = sel_er, ec2 = sel_ec, t;
		mdrag = 1;
		sel_on = 1;
		mark_on = mpend = 0;
		if (sr > er2 || (sr == er2 && sc > ec2)) {
			t = sr; sr = er2; er2 = t;
			t = sc; sc = ec2; ec2 = t;
		}
		if (!msel_mode) {	/* plain char drag: end follows pointer */
			sel_er = r;
			sel_ec = c;
		} else
		if (r < sr || (r == sr && c < sc)) {
			sel_br = r;
			sel_bc = c;
			sel_er = er2;
			sel_ec = ec2;
		} else if (r > er2 || (r == er2 && c > ec2)) {
			sel_br = sr;
			sel_bc = sc;
			sel_er = r;
			sel_ec = c;
		}
		dirty = selup = 1;
	}
	if (dirty && (r != orow || c != ocol || dirty > 1))
		mouse_refresh(selup);
}


static int mouse_bit(int fd, int type, int bit)
{
	unsigned char bits[128] = {0};
	if (ioctl(fd, EVIOCGBIT(type, sizeof(bits)), bits) < 0)
		return 0;
	return bit / 8 < (int) sizeof(bits) && (bits[bit / 8] & (1 << (bit % 8)));
}

/* open the event device at path and grab it; a device that reports an
   absolute axis also gives up the range that axis moves in */
static int mouse_try(char *path)
{
	struct input_absinfo ab;
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	int i;
	if (fd < 0)
		return -1;
	for (i = 0; i < 2; i++) {
		abs_min[i] = abs_max[i] = 0;
		if (!ioctl(fd, EVIOCGABS(ABS_X + i), &ab) &&
			ab.maximum > ab.minimum) {
			abs_min[i] = ab.minimum;
			abs_max[i] = ab.maximum;
		}
	}
	/* claim the device so other readers (getty, gpm, libinput) stay out;
	   EVIOCGRAB just treats a nonzero arg as "grab" */
	if (ioctl(fd, EVIOCGRAB, 1) < 0)
		fprintf(stderr, "nextfb: mouse: cannot grab %s\n", path);
	return fd;
}

/* the pointing device named by the MOUSE env var or macro: a node
   path like "/dev/input/event24" or a bare name under MOUSE_DIR; empty,
   the default, means there is no mouse */
static void mouse_open(void)
{
	char *want = getenv("MOUSE");
	char path[512];
	static int warned;
	if (mfd >= 0)
		return;
	if (!want)
		want = MOUSE;
	if (!*want)		/* an empty MOUSE variable turns the macro off too */
		return;
	if (*want != '/') {
		snprintf(path, sizeof(path), "%s/%s", MOUSE_DIR, want);
		want = path;
	}
	mfd = mouse_try(want);
	if (mfd < 0 && !warned++)
		fprintf(stderr, "nextfb: mouse: cannot open %s\n", want);
	if (mfd < 0)
		return;
	/* start the software cursor at screen center */
	mx = fb_cols() / 2;
	my = fb_rows() / 2;
	mrow = mcell_r();
	mcol = mcell_c();
}

static void mouse_read(void)
{
	struct input_event ev;
	while (read(mfd, &ev, sizeof(ev)) == (int) sizeof(ev))
		mouse_event(ev.type, ev.code, ev.value);
}

static int readchar(void)
{
	char b;
	if (read(0, &b, 1) > 0)
		return (unsigned char) b;
	return -1;
}

/* the current terminal */
static int cterm(void)
{
	return tops[ctag] * NTAGS + ctag;
}

/* tag's active terminal */
static int tterm(int n)
{
	return tops[n] * NTAGS + n;
}

/* the other terminal in the same tag */
static int aterm(int n)
{
	return n < NTAGS ? n + NTAGS : n - NTAGS;
}

/* the next terminal */
static int nterm(void)
{
	int n = (cterm() + 1) % NTERMS;
	while (n != cterm()) {
		if (terms[n] && terms[n]->fd)
			break;
		n = (n + 1) % NTERMS;
	}
	return n;
}

/* term struct of cterm() */
static struct term *tmain(void)
{
	return terms[cterm()]->fd ? terms[cterm()] : NULL;
}

#define BRWID		2
#define BRCLR		0xff0000

static void t_conf(int idx)
{
	int h1 = fb_rows() / 2 / pad_crows() * pad_crows();
	int h2 = fb_rows() - h1 - 4 * BRWID;
	int w1 = fb_cols() / 2 / pad_ccols() * pad_ccols();
	int w2 = fb_cols() - w1 - 4 * BRWID;
	int tag = idx % NTAGS;
	int top = idx < NTAGS;
	if (split[tag] == 0)
		pad_conf(0, 0, fb_rows(), fb_cols());
	if (split[tag] == 1)
		pad_conf(top ? BRWID : h1 + 3 * BRWID, BRWID,
			top ? h1 : h2, fb_cols() - 2 * BRWID);
	if (split[tag] == 2)
		pad_conf(BRWID, top ? BRWID : w1 + 3 * BRWID,
			fb_rows() - 2 * BRWID, top ? w1 : w2);
}

static void t_hide(int idx, int save)
{
	if (save && terms[idx]->fd && TERMSNAP(idx))
		scr_snap(idx);
	term_save(terms[idx]);
}

/* show=0 (hidden), show=1 (visible), show=2 (load), show=3 (redraw) */
static int t_show(int idx, int show)
{
	t_conf(idx);
	term_load(terms[idx], show > 0);
	if (show == 2)	/* redraw if scr_load() fails */
		show += !terms[idx]->fd || !TERMSNAP(idx) || scr_load(idx);
	if (show > 0)
		term_redraw(show == 3);
	return show;
}

/* switch active terminal; hide oidx and show nidx */
static int t_hideshow(int oidx, int save, int nidx, int show)
{
	int otag = oidx % NTAGS;
	int ntag = nidx % NTAGS;
	int ret;
	t_hide(oidx, save);
	if (!terms[nidx]) {
		pad_pset(ipstate);
		pad_conf(0, 0, fb_rows(), fb_cols());
		terms[nidx] = term_make();
	}
	if (show && split[otag] && otag == ntag)
		pad_border(0, BRWID);
	pad_pset(terms[nidx]->ps);
	ret = t_show(nidx, show);
	if (show && split[ntag])
		pad_border(BRCLR, BRWID);
	return ret;
}

static void listtags(void)
{
	/* colors for tags based on their number of terminals */
	int fg = 0x96cb5c, bg = 0x516f7b;
	int colors[] = {0x173f4f, fg, 0x68cbc0 | FN_B};
	int c = 0;
	int r = pad_rows() - 1;
	int i;
	pad_put('T', r, c++, fg | FN_B, bg);
	pad_put('A', r, c++, fg | FN_B, bg);
	pad_put('G', r, c++, fg | FN_B, bg);
	pad_put('S', r, c++, fg | FN_B, bg);
	pad_put(':', r, c++, fg | FN_B, bg);
	pad_put(' ', r, c++, fg | FN_B, bg);
	for (i = 0; i < NTAGS && c + 2 < pad_cols(); i++) {
		int nt = 0;
		if (terms[i] && terms[i]->fd)
			nt++;
		if (tterm(i) == aterm(i))
			nt++;
		pad_put(i == ctag ? '(' : ' ', r, c++, fg, bg);
		if (TERMSNAP(i))
			pad_put(tags[i], r, c++, !nt ? bg : colors[nt], colors[0]);
		else
			pad_put(tags[i], r, c++, colors[nt], bg);
		pad_put(i == ctag ? ')' : ' ', r, c++, fg, bg);
	}
	for (; c < pad_cols(); c++)
		pad_put(' ', r, c, fg, bg);
	tagslisted = 1;
}

/* set cterm() */
static void t_set(int n)
{
	int i = cterm();
	if (i == n || cmdmode)
		return;
	if (taglock && ctag != n % NTAGS)
		return;
	if (ctag != n % NTAGS)
		ltag = ctag;
	if (ctag == n % NTAGS) {
		if (split[n % NTAGS])
			t_hideshow(i, 0, n, 1);
		else
			t_hideshow(i, 1, n, 2);
	} else {
		int draw = t_hideshow(i, 1, n, 2);
		if (split[n % NTAGS]) {
			t_hideshow(n, 0, aterm(n), draw == 2 ? 1 : 2);
			t_hideshow(aterm(n), 0, n, 1);
		}
	}
	if (!terms[i]->fd) {
		if (terms[i]->ps != ipstate)
			pad_free(terms[i]->ps);
		term_free(terms[i]);
		terms[i] = NULL;
	}
	ctag = n % NTAGS;
	tops[ctag] = n / NTAGS;
	if (!terms[cterm()]->hpos)
		listtags();
}

static void t_split(int n)
{
	split[ctag] = n;
	t_hideshow(cterm(), 0, aterm(cterm()), 3);
	t_hideshow(aterm(cterm()), 1, cterm(), 3);
}

static void t_exec(char **args)
{
	if (!tmain())
		term_exec(args);
}

static void directkey(void)
{
	static int yank;
	static char *input_buf;
	static int input_len;
	static int input_sz;
	char *s;
	int c = readchar();
	/* any keystroke clears the mouse cursor, marker and selection */
	if (mrow >= 0 || sel_on || mark_on) {
		mclear();
	}
	if (tagslisted) {
		term_redraw(1);
		tagslisted = 0;
	}
	if (*PASS && locked) {
		if (c == '\r') {
			pass[passlen] = '\0';
			if (!strcmp(PASS, pass))
				locked = 0;
			passlen = 0;
			return;
		}
		if (isprint(c) && passlen + 1 < sizeof(pass))
			pass[passlen++] = c;
		return;
	}
	#define bufchk() \
	if (input_len + 1 >= input_sz) { \
		input_sz += 128; \
		input_buf = realloc(input_buf, input_sz); \
	} \

	if (c == ESC) {
		switch ((c = readchar())) {
		case 'c':
			t_exec((char*[])SHELL);
			return;
		case 'm':
			t_exec((char*[])MAIL);
			return;
		case 'e':
			t_exec((char*[])EDITOR);
			return;
		case 't':;
			int i = cterm();
			if (terms[i]->ps != ipstate)
				pad_free(terms[i]->ps);
			if (!pad_init()) {
				exitit = 1;
				return;
			}
			pad_pset(pstate);
			int pid = terms[i]->pid;
			int fd = terms[i]->fd;
			term_free(terms[i]);
			terms[i] = term_make();
			terms[i]->pid = pid;
			terms[i]->fd = fd;
			misc_save(&terms[i]->cur);
			term_load(terms[i], 1);
			term_redraw(1);
			term_send('');
			return;
		case 'b':
		case 'v':
			/* a register filled by the yank search holds suggestions, so
			   only its first line is pasteable; a register filled by a
			   selection holds text, so paste every line of it; the test
			   on *s also covers a register cleared meanwhile */
			for (s = yank_buf; s && *s && (yank_all || *s != 0x0a); s++)
				term_send(*s);
			if (c == 'v')
				return;
		case 'y':
			endyank:
			yank = !yank;
			input_len = 0;
			term_redraw(1);
			return;
		case 'u':
		case 'n':
			if (!yank)
				return;
			input_len = 0;
			s = yank_buf;
			while (c == 'n' && s && *s != '\n') {
				bufchk()
				input_buf[input_len++] = *s++;
			}
			goto yank_update;
		case 'j':
		case 'k':
			t_set(aterm(cterm()));
			return;
		case 'o':
			t_set(tterm(ltag));
			return;
		case 'p':
			listtags();
			return;
		case '\t':
			if (nterm() != cterm())
				t_set(nterm());
			return;
		case 'q':
			exitit = 1;
			return;
		case 's':
			term_screenshot();
			return;
		case 'l':
			term_redraw(1);
			return;
		case CTRLKEY('l'):
			locked = 1;
			passlen = 0;
			return;
		case CTRLKEY('o'):
			taglock = 1 - taglock;
			return;
		case ',':
			term_scrl(pad_rows() / 2);
			return;
		case '.':
			term_scrl(-pad_rows() / 2);
			return;
		case '=':
			t_split(split[ctag] == 1 ? 2 : 1);
			return;
		case '-':
			t_split(0);
			return;
		default:
			if (strchr(tags, c)) {
				t_set(tterm(strchr(tags, c) - tags));
				return;
			}
			if (tmain())
				term_send(ESC);
		}
	} else if (yank) {
		bufchk()
		if (c == 13)
			goto endyank;
		else if (c == 127)
			input_len = input_len ? input_len - 1 : 0;
		else
			input_buf[input_len++] = c;
		yank_update:
		input_buf[input_len] = '\0';
		free(yank_buf);
		yank_buf = NULL;
		yank_set(term_yank(input_buf), 0);
		return;
	}
	if (c != -1 && tmain())
		term_send(c);
}

static void peepterm(int termid)
{
	int visible = !hidden && ctag == (termid % NTAGS) && split[ctag];
	if (termid != cterm())
		t_hideshow(cterm(), 0, termid, visible);
}

static void peepback(int termid)
{
	if (termid != cterm())
		t_hideshow(termid, 0, cterm(), !hidden);
}

static void sig_work(void);	/* defined below with the signal code */
static int pollterms(void)
{
	struct pollfd ufds[NTERMS + 3];
	int term_idx[NTERMS + 1];
	int i;
	int n = 1;
	ufds[0].fd = 0;
	ufds[0].events = POLLIN;
	for (i = 0; i < NTERMS; i++) {
		if (terms[i] && terms[i]->fd) {
			ufds[n].fd = terms[i]->fd;
			ufds[n].events = POLLIN;
			term_idx[n++] = i;
		}
	}

	/* retry if the device failed or vanished; poll timeout paces this.
	   The mouse is let go of while another console is on screen */
	if (mfd < 0 && !hidden)
		mouse_open();
	int mi = -1;
	if (mfd >= 0) {
		ufds[n].fd = mfd;
		ufds[n].events = POLLIN;
		mi = n++;
	}
	/* the signal handler only pokes this pipe, the work is done here */
	int si = -1;
	if (sigpipe[0] >= 0) {
		ufds[n].fd = sigpipe[0];
		ufds[n].events = POLLIN;
		si = n++;
	}
	int tmo = 1000;
	if (mpend) {
		tmo = (int) (mtime + CLICK_REG_MS - mnow());
		if (tmo < 20)
			tmo = 20;
	}
	if (poll(ufds, n, tmo) < 0)	/* a signal: nothing was reported */
		for (i = 0; i < n; i++)
			ufds[i].revents = 0;
	if (si >= 0 && ufds[si].revents & POLLIN) {
		char buf[64];
		while (read(sigpipe[0], buf, sizeof(buf)) > 0)
			;
	}
	sig_work();		/* goes by the flags, the pipe only wakes poll up */
	mouse_tick();		/* a busy pty must not starve the marker */

	if (ufds[0].revents & (POLLFLAGS & ~POLLIN))
		return 1;
	if (ufds[0].revents & POLLIN)
		directkey();

	if (mi >= 0 && ufds[mi].revents & (POLLFLAGS & ~POLLIN)) {
		close(mfd);
		mfd = -1;
		mi = -1;
	} else if (mi >= 0 && ufds[mi].revents & POLLIN)
		mouse_read();
	for (i = 1; i < n; i++) {

		if (i == mi || i == si)
			continue;
		if (ufds[i].revents & (POLLFLAGS & ~POLLIN))
			continue;
		if (ufds[i].revents & POLLIN) {
			peepterm(term_idx[i]);
			term_read();
			peepback(term_idx[i]);
		}
	}
	return 0;
}

static void mainloop(char **args)
{
	struct termios oldtermios, termios;
	tcgetattr(0, &termios);
	oldtermios = termios;
	termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
		| INLCR | IGNCR | ICRNL | IXON);
	termios.c_oflag &= ~OPOST;
	termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	termios.c_cflag &= ~(CSIZE | PARENB);
	termios.c_cflag |= CS8;
	tcsetattr(0, TCSAFLUSH, &termios);
	term_load(terms[cterm()], 1);
	term_redraw(1);
	if (args) {
		cmdmode = 1;
		t_exec(args);
	}

	mouse_open();
	while (!exitit)
		if (pollterms())
			break;
	tcsetattr(0, 0, &oldtermios);
}

/* KD_GRAPHICS keeps fbcon from drawing text into the pixels fb paints;
   the mode is the console's and outlives the process, so every way out
   has to put KD_TEXT back */
static void console_text(void)
{
	if (GRAPHIC_CONSOLE)
		ioctl(0, KDSETMODE, KD_TEXT);
}

static void console_graphics(void)
{
	if (GRAPHIC_CONSOLE)
		ioctl(0, KDSETMODE, KD_GRAPHICS);
}

static void vt_acquire(void)
{
	console_graphics();
	hidden = 0;
	fb_cmap();
	if (t_show(cterm(), 2) == 3 && split[ctag]) {
		t_hideshow(cterm(), 0, aterm(cterm()), 3);
		t_hideshow(aterm(cterm()), 0, cterm(), 1);
	}
}

static void vt_release(void)
{
	if (hidden)		/* not on screen: nothing to give up */
		return;
	/* the pointer and the mouse go with the screen: the other console
	   gets the device, and no click is taken for a terminal out of sight */
	mclear();
	if (mfd >= 0)
		close(mfd);
	mfd = -1;
	mbprev = 0;
	hidden = 1;
	t_hide(cterm(), 1);
	console_text();
	/* refused when no switch is pending, a stray signal: fb is still
	   the one on screen, so take it back instead of staying blind */
	if (ioctl(0, VT_RELDISP, 1) < 0)
		vt_acquire();
}

static void sig_work(void)
{
	if (sig_quit)
		exitit = 1;
	if (exitit)
		return;
	/* the kernel sends no acquire until a release is answered, and that
	   is done here; both flags set can only be a switch back followed by
	   a switch away, so the acquire goes first */
	if (sig_acq) {
		sig_acq = 0;
		vt_acquire();
	}
	if (sig_rel) {
		sig_rel = 0;
		vt_release();
	}
	if (sig_child) {
		int pid, i;
		sig_child = 0;
		while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
			if (terms[cterm()]->pid == pid) {
				mclear();	/* what was selected is gone */
				term_end();
			}
			for (i = 0; i < NTERMS; i++) {
				if (terms[i] && terms[i]->pid == pid) {
					terms[i]->fd = 0;
					terms[i]->pid = 0;
					scr_free(i);
					break;
				}
			}
		}
		if (cmdmode)
			exitit = 1;
	}
}

/* async-signal-safe: set a flag, wake the main loop up through the pipe */
static void signalreceived(int n)
{
	int err = errno;
	if (n == SIGUSR1)
		sig_rel = 1;
	else if (n == SIGUSR2)
		sig_acq = 1;
	else if (n == SIGCHLD)
		sig_child = 1;
	else
		sig_quit = 1;
	if (sigpipe[1] >= 0)
		while (write(sigpipe[1], "", 1) < 0 && errno == EINTR)
			;
	errno = err;
}

/* a crash must not leave the console in graphics mode; the handler is
   one-shot, so the signal raised again takes its default course */
static void sig_fatal(int n)
{
	console_text();
	raise(n);
}

/* sigaction() and not signal(): built with -std=c99, glibc's signal() is
   the System V one, which uninstalls the handler on its first delivery,
   so the second console switch would kill fb */
static void sig_on(int n, void (*fn)(int), int flags)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = fn;
	sa.sa_flags = flags;
	sigemptyset(&sa.sa_mask);
	sigaction(n, &sa, NULL);
}

static void signalsetup(void)
{
	static int quit[] = {SIGTERM, SIGHUP, SIGINT};
	static int fatal[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGQUIT};
	struct vt_mode vtm;
	unsigned int i;
	if (pipe(sigpipe) == 0) {
		fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
		fcntl(sigpipe[1], F_SETFL, O_NONBLOCK);
		fcntl(sigpipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(sigpipe[1], F_SETFD, FD_CLOEXEC);
	} else {
		sigpipe[0] = sigpipe[1] = -1;
	}
	sig_on(SIGUSR1, signalreceived, SA_RESTART);
	sig_on(SIGUSR2, signalreceived, SA_RESTART);
	sig_on(SIGCHLD, signalreceived, SA_RESTART | SA_NOCLDSTOP);
	for (i = 0; i < LEN(quit); i++)
		sig_on(quit[i], signalreceived, SA_RESTART);
	for (i = 0; i < LEN(fatal); i++)
		sig_on(fatal[i], sig_fatal, SA_RESETHAND);
	vtm.mode = VT_PROCESS;
	vtm.waitv = 0;
	vtm.relsig = SIGUSR1;
	vtm.acqsig = SIGUSR2;
	vtm.frsig = 0;
	if (ioctl(0, VT_SETMODE, &vtm) < 0)
		fprintf(stderr,
			"fb: cannot take over the virtual console, run it"
			" on the terminal it owns\n");
}

int main(int argc, char **argv)
{
	char *hide = "\x1b[2J\x1b[H\x1b[?25l";
	char *show = "\x1b[?25h";
	char **args = argv + 1;
	int i;
	if (fb_init(getenv("FBDEV"))) {
		fprintf(stderr, "fbpad: failed to initialize the framebuffer\n");
		return 1;
	}
	if (!(ipstate = pad_init())) {
		fprintf(stderr, "fbpad: cannot find fonts\n");
		return 1;
	}
	terms[0] = term_make();
	write(1, hide, strlen(hide));
	signalsetup();
	console_graphics();
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	while (args[0] && args[0][0] == '-')
		args++;
	mainloop(args[0] ? args : NULL);
	console_text();
	write(1, show, strlen(show));
	for (i = 0; i < NTERMS; i++)
		term_free(terms[i]);
	pad_free(ipstate);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~O_NONBLOCK);
	scr_done();
	fb_free();
	return 0;
}
