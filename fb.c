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
#include <dirent.h>
#include <linux/input.h>
#include <unistd.h>
#include <linux/vt.h>
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


/* ---- mouse support ---- */

#define MOUSE_DIR	"/dev/input"
#define WHEEL_STEP	3

static int mouse_bit(int fd, int type, int bit);

static int mfd = -1;		/* current mouse input event device */
static int mx, my;		/* software mouse cursor position (pixels) */
static int mbprev;		/* previously pressed mouse buttons */
static long long mtime;		/* time of the last button press (ms) */
static int mlast_r, mlast_c;	/* cell of the previous click */
static int mclick;		/* consecutive click count on same cell */
static int sel_on;		/* a mouse selection is active */
static char *yank_buf;	/* yank register: mouse selections and ^Qy search */
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
	int r, c, n = pad_cols() * 5;
	char *buf = malloc((er - br + 1) * n);
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

static void mpaste(void)
{
	char *s, *buf;
	if (!sel_on)
		return;
	buf = s = mtext(sel_br, sel_bc, sel_er, sel_ec);
	if (!s)
		return;
	while (*s)
		term_send(*s++);
	free(buf);
}

static void mclick_ev(int r, int c)
{
	long long now = mnow();
	if (r == mlast_r && c == mlast_c && now - mtime < 400)
		mclick++;
	else
		mclick = 1;
	mtime = now;
	mlast_r = r;
	mlast_c = c;
	if (mclick >= 3)
		msel_line(r);
	else if (mclick == 2)
		msel_word(r, c);
	else {
		sel_br = sel_er = r;
		sel_bc = sel_ec = c;
		sel_on = 1;
	}
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
	if (!e) {
		free(s);
		return;
	}
	free(yank_buf);
	yank_buf = s;
}

/* hand the current cursor cell and selection to the term layer */
static void mouse_refresh(void)
{
	int br = sel_br, bc = sel_bc, er = sel_er, ec = sel_ec, t;
	if (sel_on && (br > er || (br == er && bc > ec))) {
		t = br; br = er; er = t;
		t = bc; bc = ec; ec = t;
	}
	term_mouse(mcell_r(), mcell_c(), sel_on, br, bc, er, ec);
	msel_yank();
}

static void mouse_event(int type, int code, int value)
{
	int r = mcell_r(), c = mcell_c();
	int orow = mrow, ocol = mcol, dirty = 0;
	switch (type) {
	case EV_REL:
		if (code == REL_X || code == REL_Y) {
			dirty = 1;
			if (code == REL_X)
				mx = LIMIT(mx + value, 0, fb_cols() - 1);
			else
				my = LIMIT(my + value, 0, fb_rows() - 1);
		} else if (code == REL_WHEEL || code == REL_WHEEL_HI_RES ||
			(code == REL_HWHEEL && !mouse_bit(mfd, EV_REL, REL_WHEEL)))
			term_scrl(value > 0 ? WHEEL_STEP : -WHEEL_STEP);
		break;
	case EV_ABS:
		dirty = 1;
		if (code == ABS_X)
			mx = LIMIT(value, 0, fb_cols() / 2 - 1);
		else if (code == ABS_Y)
			my = LIMIT(value, 0, fb_rows() / 2 - 1);
		break;
	case EV_KEY:
		if (code == BTN_LEFT) {
			if (value && !(mbprev & 1))
				mclick_ev(r, c);
			mbprev = value ? mbprev | 1 : mbprev & ~1;
			dirty = 1;
		} else if (code == BTN_RIGHT) {
			if (value && !(mbprev & 2)) {
				sel_er = r;	/* set end of selection */
				sel_ec = c;
				sel_on = 1;
			}
			mbprev = value ? mbprev | 2 : mbprev & ~2;
			dirty = 1;
		} else if (code == BTN_MIDDLE) {
			if (value && !(mbprev & 4))
				mpaste();
			mbprev = value ? mbprev | 4 : mbprev & ~4;
			dirty = 1;
		}
		break;
	}
	if (type == EV_KEY && value)
		dirty = 2;	/* a click repaints at once, no motion needed */
	/* left button held down: dragging extends the selection */
	if ((mbprev & 1) && type != EV_KEY) {
		sel_er = r;
		sel_ec = c;
		sel_on = 1;
		dirty = 1;
	}
	if (dirty && (r != orow || c != ocol || dirty > 1))
		mouse_refresh();
}



static int mouse_bit(int fd, int type, int bit)
{
	unsigned char bits[128] = {0};
	if (ioctl(fd, EVIOCGBIT(type, sizeof(bits)), bits) < 0)
		return 0;
	return bit / 8 < (int) sizeof(bits) && (bits[bit / 8] & (1 << (bit % 8)));
}


/* score how mouse-like this event device is; 0 means not a pointer */
static int mouse_score(int fd)
{
	int btn = mouse_bit(fd, EV_KEY, BTN_MOUSE) ||
			mouse_bit(fd, EV_KEY, BTN_LEFT);
	int rel = mouse_bit(fd, EV_REL, REL_X);
	int abs = mouse_bit(fd, EV_ABS, ABS_X);
	int wheel = mouse_bit(fd, EV_REL, REL_WHEEL);
	if (!btn || !(rel || abs))
		return 0;
	return 1 + rel + wheel * 2;	/* relative + wheel mice rank highest */
}

/* open an event device; validate as a pointer unless FORCE */
static int mouse_try(char *path, int force)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return -1;
	if (!force && mouse_score(fd) <= 0) {
		close(fd);
		return -1;
	}
	/* claim the device so other readers (getty, gpm, libinput) stay out;
	   EVIOCGRAB just treats a nonzero arg as "grab" */
	if (ioctl(fd, EVIOCGRAB, 1) < 0)
		fprintf(stderr, "nextfb: mouse: cannot grab %s\n", path);
	return fd;
}


/* open a pointing device: honor the MOUSE env/macro, else auto-detect */
static void mouse_open(void)
{
	DIR *d;
	struct dirent *de;
	char *want = getenv("MOUSE");
	char path[512];
	int best = -1, bestscore = 0;
	if (mfd >= 0)
		return;
	/* start the software cursor at screen center */
	mx = fb_cols() / 2;
	my = fb_rows() / 2;
	mrow = mcell_r();
	mcol = mcell_c();
	if (!want)
		want = MOUSE[0] ? MOUSE : NULL;
	if (want) {
		char *p = want, path2[512];
		static int warned;
		if (*p != '/') {
			snprintf(path2, sizeof(path2), "%s/%s", MOUSE_DIR,
					p);
			p = path2;
		}
		if ((mfd = mouse_try(p, 1)) >= 0)
			return;
		if (!warned++)
			fprintf(stderr, "nextfb: mouse: cannot open %s\n",
				p);
	}
	if (!(d = opendir(MOUSE_DIR)))
		return;
	while ((de = readdir(d)))
		if (!strncmp(de->d_name, "event", 5)) {
			int fd, sc;
			snprintf(path, sizeof(path), "%s/%s", MOUSE_DIR,
					de->d_name);
			fd = open(path, O_RDONLY | O_NONBLOCK);
			if (fd < 0)
				continue;
			sc = mouse_score(fd);
			if (sc > bestscore) {
				if (best >= 0)
					close(best);
				best = fd;
				bestscore = sc;
			} else
				close(fd);
		}
	closedir(d);
	mfd = best;
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
	/* any keystroke clears the mouse cursor and selection */
	if (mrow >= 0 || sel_on) {
		sel_on = 0;
		term_mouse(-1, 0, 0, 0, 0, 0, 0);
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
			s = yank_buf;
			while (s && *s != '\n')
				term_send(*s++);
			if (c == 'v')
				return;
		case 'y':
			endyank:
			yank = !yank;
			input_len = 0;
			input_sz = 0;
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
		yank_buf = term_yank(input_buf);
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

static int pollterms(void)
{
	struct pollfd ufds[NTERMS + 2];
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

	/* retry if the device failed or vanished; poll timeout paces this */
	if (mfd < 0)
		mouse_open();
	int mi = -1;
	if (mfd >= 0) {
		ufds[n].fd = mfd;
		ufds[n].events = POLLIN;
		mi = n++;
	}
	if (poll(ufds, n, 1000) < 1)
		return 0;
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

		if (i == mi)
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

static void signalreceived(int n)
{
	if (exitit)
		return;
	switch (n) {
	case SIGUSR1:
		hidden = 1;
		t_hide(cterm(), 1);
		ioctl(0, VT_RELDISP, 1);
		break;
	case SIGUSR2:
		hidden = 0;
		fb_cmap();
		if (t_show(cterm(), 2) == 3 && split[ctag]) {
			t_hideshow(cterm(), 0, aterm(cterm()), 3);
			t_hideshow(aterm(cterm()), 0, cterm(), 1);
		}
		break;
	case SIGCHLD:;
		int pid, i;
		while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
			if (terms[cterm()]->pid == pid)
				term_end();
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
		break;
	}
}

static void signalsetup(void)
{
	struct vt_mode vtm;
	vtm.mode = VT_PROCESS;
	vtm.waitv = 0;
	vtm.relsig = SIGUSR1;
	vtm.acqsig = SIGUSR2;
	vtm.frsig = 0;
	signal(SIGUSR1, signalreceived);
	signal(SIGUSR2, signalreceived);
	signal(SIGCHLD, signalreceived);
	ioctl(0, VT_SETMODE, &vtm);
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
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	while (args[0] && args[0][0] == '-')
		args++;
	mainloop(args[0] ? args : NULL);
	write(1, show, strlen(show));
	for (i = 0; i < NTERMS; i++)
		term_free(terms[i]);
	pad_free(ipstate);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~O_NONBLOCK);
	scr_done();
	fb_free();
	return 0;
}
