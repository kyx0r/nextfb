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
#define RED1		0xff0000
#define GREEN2		0x33ff00
#define YELLOW3		0xffff00
#define BLUE4		0x0066ff
#define MAGENTA5	0xcc00ff
#define CYAN6		0x00ffff
#define WHITE7		0xffffff
/* bright colors */
#define BLACK8		0x808080
#define RED9		0xff0000
#define GREEN10		0x33ff00
#define YELLOW11	0xffff00
#define BLUE12		0x0066ff
#define MAGENTA13	0xcc00ff
#define CYAN14		0x00ffff
#define WHITE15		0xffffff
