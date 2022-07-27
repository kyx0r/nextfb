#ifndef PWD
#define PWD		"."
#endif

/* list of tags */
#define TAGS		"1234567890"
#define TAGS_SAVED	""

/* tinyfont files for regular, italic, and bold fonts */
#define FR		PWD"/ar.tf"
#define FI		PWD"/ai.tf"
#define FB		PWD"/ab.tf"

/* programs mapped to m-c, m-m, m-e */
#define SHELL		{"sh", NULL}
#define EDITOR		{"vi", NULL}
#define MAIL		{"mailx", "-f", "+inbox", NULL}

/* TERM variable for launched programs */
#define TERM		"linux"

/* foreground and background colors */
#define FGCOLOR		WHITE15
#define BGCOLOR		BLACK0

/* where to write the screen shot */
#define SCRSHOT		"/tmp/scr"

/* lock command password; NULL disables locking */
#define PASS		""

/* optimized version of fb_val() */
#define FB_VAL(r, g, b)	fb_val((r), (g), (b))

/* brighten colors 0-7 for bold text */
#define BRIGHTEN	1

#define BLACK0		0x000000
#define RED1		0xaa0000
#define GREEN2		0x00aa00
#define YELLOW3		0xaa5500
#define BLUE4		0x0000aa
#define MAGENTA5	0xaa00aa
#define CYAN6		0x00aaaa
#define WHITE7		0xaaaaaa
/* bright colors */
#define BLACK8		0x555555
#define RED9		0xff5555
#define GREEN10		0x55ff55
#define YELLOW11	0xffff55
#define BLUE12		0x5555ff
#define MAGENTA13	0xff55ff
#define CYAN14		0x55ffff
#define WHITE15		0xffffff
