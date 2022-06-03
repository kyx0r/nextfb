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
#include <unistd.h>
#include <linux/vt.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include "fbpad.h"
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

static void reverse_in_place(char *str, int len)
{
	char *p1 = str;
	char *p2 = str + len - 1;
	while (p1 < p2) {
		char tmp = *p1;
		*p1++ = *p2;
		*p2-- = tmp;
	}
}

char *itoa(int n, char s[])
{
	int i = 0, sign;
	if ((sign = n) < 0)		/* record sign */
		n = -n;			/* make n positive */
	do {				/* generate digits in reverse order */
		s[i++] = n % 10 + '0';	/* get next digit */
	} while ((n /= 10) > 0);	/* delete it */
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	reverse_in_place(s, i);
	return &s[i];
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
		if (terms[n]->fd)
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
	if (show && split[otag] && otag == ntag)
		pad_border(0, BRWID);
	pad_pset(terms[nidx]->ps);
	ret = t_show(nidx, show);
	if (show && split[ntag])
		pad_border(BRCLR, BRWID);
	return ret;
}

/* set cterm() */
static void t_set(int n)
{
	if (cterm() == n || cmdmode)
		return;
	if (taglock && ctag != n % NTAGS)
		return;
	if (ctag != n % NTAGS)
		ltag = ctag;
	if (ctag == n % NTAGS) {
		if (split[n % NTAGS])
			t_hideshow(cterm(), 0, n, 1);
		else
			t_hideshow(cterm(), 1, n, 2);
	} else {
		int draw = t_hideshow(cterm(), 1, n, 2);
		if (split[n % NTAGS]) {
			t_hideshow(n, 0, aterm(n), draw == 2 ? 1 : 2);
			t_hideshow(aterm(n), 0, n, 1);
		}
	}
	ctag = n % NTAGS;
	tops[ctag] = n / NTAGS;
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
		if (terms[i]->fd)
			nt++;
		if (terms[aterm(i)]->fd)
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
}

static void directkey(void)
{
	static int tfsz;
	static int yank;
	static char *yank_buf;
	static int yank_len;
	static int yank_sz;
	int c = readchar();
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
		case 't':
			tfsz = !tfsz;
			return;
		case 'v':
			for (c = 0; c < yank_len; c++)
				term_send(yank_buf[c]);
			return;
		case 'b':
			for (c = yank_len - 1; c >= 0; c--)
				term_send(yank_buf[c]);
			return;
		case 'x':
			yank_len = 0;
			if (yank)
				goto yankit;
		case 'y':
			yank = !yank;
			if (yank)
				misc_save(&terms[cterm()]->cur);
			else {	/* restore old cursor pos */
				misc_load(&terms[cterm()]->cur);
				term_yank(0);
				return;
			}
			if (!yank_buf) {
				yank_sz = 128;
				yank_buf = malloc(yank_sz);
			}
			goto yankit;
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
	} else if (tfsz) {
		char *tf1[] = TFGENFR;
		char *tf2[] = TFGENFI;
		char *tf3[] = TFGENFB;
		char **tfgen = tfsz == 1 ? tf1 : tfsz == 2 ? tf2 : tf3;
		char height[] = "-h    ";
		char width[] = "-w     ";
		char size[] = "               ";
		static int tfh, tfw, tfs;
		if (c == 'j' || c == 'k') {
			c == 'k' ? --tfh : ++tfh;
		} else if (c == 'h' || c == 'l') {
			c == 'l' ? --tfw : ++tfw;
		} else if (c == 'u' || c == 'i') {
			c == 'i' ? --tfs : ++tfs;
		} else if (c == 'n') {
			tfsz = tfsz > 3 ? 1 : tfsz+1;
			return;
		} else
			return;
		tfh = tfh <= 1 ? pad_crows() : tfh;
		tfw = tfw <= 1 ? pad_ccols() : tfw;
		tfs = tfs <= 1 ? atoi(tfgen[6]) : tfs;
		itoa(tfh, height+2);
		itoa(tfw, width+2);
		strcpy(itoa(tfs, size), tfgen[6]+2);
		tfgen[1] = height;
		tfgen[2] = width;
		tfgen[6] = size;
		spawn(tfgen);
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
	} else if (yank) {
		yankit:
		if (yank_len + 1 == yank_sz) {
			yank_sz *= 2;
			yank_buf = realloc(yank_buf, yank_sz);
		}
		yank_buf[yank_len++] = term_yank(c);
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
	struct pollfd ufds[NTERMS + 1];
	int term_idx[NTERMS + 1];
	int i;
	int n = 1;
	ufds[0].fd = 0;
	ufds[0].events = POLLIN;
	for (i = 0; i < NTERMS; i++) {
		if (terms[i]->fd) {
			ufds[n].fd = terms[i]->fd;
			ufds[n].events = POLLIN;
			term_idx[n++] = i;
		}
	}
	if (poll(ufds, n, 1000) < 1)
		return 0;
	if (ufds[0].revents & (POLLFLAGS & ~POLLIN))
		return 1;
	if (ufds[0].revents & POLLIN)
		directkey();
	for (i = 1; i < n; i++) {
		if (!(ufds[i].revents & POLLFLAGS))
			continue;
		peepterm(term_idx[i]);
		if (ufds[i].revents & POLLIN) {
			term_read();
		} else {
			scr_free(term_idx[i]);
			term_end();
			if (cmdmode)
				exitit = 1;
		}
		peepback(term_idx[i]);
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
	case SIGCHLD:
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
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
	for (i = 0; i < NTERMS; i++)
		terms[i] = term_make();
	write(1, hide, strlen(hide));
	signalsetup();
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	while (args[0] && args[0][0] == '-')
		args++;
	int errfd = open("/dev/null", O_WRONLY);
	dup2(errfd, 2);
	mainloop(args[0] ? args : NULL);
	write(1, show, strlen(show));
	for (i = 0; i < NTERMS; i++)
		term_free(terms[i]);
	pad_free(ipstate);
	scr_done();
	fb_free();
	close(errfd);
	return 0;
}
