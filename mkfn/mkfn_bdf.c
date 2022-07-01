#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define QUOTE(s) #s
#define QUOTE_MACRO(s) QUOTE(s)
#define KEYMAX 64
#define SEPMAX 16
#define VALMAX 128

static char skey[KEYMAX+1];
static char ssep[SEPMAX+1];
static char sval[VALMAX+1];

typedef struct metadata {
	int w, h, x, y, n;
} metadata;

typedef struct tinyfont {
	char sig[8];	/* tinyfont signature; "tinyfont" */
	int ver;	/* version; 0 */
	int n;		/* number of glyphs */
	int rows, cols;	/* glyph dimensions */
} tinyfont;

static int get_val(FILE *fp, const char *key)
{
	do {
		if (fscanf(fp, "%" QUOTE_MACRO(KEYMAX) "s", skey) == EOF)
			return 1;
		fscanf(fp, "%" QUOTE_MACRO(SEPMAX) "[ \t\n]", ssep);
		if (strchr(ssep, '\n'))
			strcpy(sval, "");
		else
			fscanf(fp, "%" QUOTE_MACRO(VALMAX) "[^\n]\n", sval);
	} while (strcmp(key, skey));
	return 0;
}

int bdf_info(FILE *fp, metadata *md)
{
	if (get_val(fp, "STARTFONT"))
		return 1;
	get_val(fp, "FONTBOUNDINGBOX");
	sscanf(sval, "%d %d %d %d", &md->w, &md->h, &md->x, &md->y);
	get_val(fp, "CHARS");
	sscanf(sval, "%d", &md->n);
	return 0;
}

static int comp_int(const void *a, const void *b)
{
	const int *arg1 = a;
	const int *arg2 = b;
	return *arg1 - *arg2;
}

int *bdf_glyphs(FILE *fp, metadata *md)
{
	int *codes;
	if (!md->n)
		bdf_info(fp, md);
	codes = malloc(md->n * sizeof(*codes));
	for (int i = 0; i < md->n; i++) {
		get_val(fp, "ENCODING");
		sscanf(sval, "%d", &codes[i]);
	}
	qsort(codes, md->n, sizeof(*codes), comp_int);
	return codes;
}

int main(int argc, char *argv[])
{
	FILE *fp;
	metadata md;
	tinyfont hd;
	int n, i, j, x;
	int fd;
	int gw, gh, gx, gy;
	char *canvas;

	if (argc != 3) {
		fprintf(stderr, "usage: %s input.bdf output.tf\n", argv[0]);
		return 1;
	}
	fp = fopen(argv[1], "r");
	if (!fp) {
		fprintf(stderr, "could not open file %s\n", argv[1]);
		return 1;
	}
	fd = creat(argv[2], 0666);
	if (fd == -1) {
		fprintf(stderr, "could not create file %s\n", argv[2]);
		return 1;
	}
	if (bdf_info(fp, &md)) {
		fprintf(stderr, "%s is not a BDF file\n", argv[1]);
		return 1;
	}
	char *sig = "tinyfont";
	memcpy(hd.sig, sig, strlen(sig));
	hd.ver = 0;
	hd.rows = md.h;
	hd.cols = md.w;
	hd.n = md.n;
	write(fd, &hd, sizeof(hd));
	printf("%d %d %d\n", md.n, md.h, md.w);
	canvas = malloc(md.h * md.w);
	int *gl = bdf_glyphs(fp, &md);
	if (!gl || !canvas) {
		fprintf(stderr, "could not allocate memory\n");
		return 1;
	}
	write(fd, gl, sizeof(gl[0]) * md.n);
	rewind(fp);
	for (n = 0; n < md.n; n++) {
		int code;
		unsigned long int byte;
		char hex[sizeof(byte)+2] = {0};
		get_val(fp, "ENCODING");
		sscanf(sval, "%d", &code);
		get_val(fp, "BBX");
		sscanf(sval, "%d %d %d %d", &gw, &gh, &gx, &gy);
		get_val(fp, "BITMAP");
		memset(canvas, 0, md.h * md.w);
		for (i = 0; i < gh; i++) {
			char *hp = hex;
			while (hp < hex+sizeof(hex)) {
				fread(hp, 1, 1, fp);
				if (*hp == '\n') {
					*hp = '\0';
					break;
				} else
					hp++;
			}
			byte = (unsigned long int) strtol(hex, NULL, 16);
			for (x = gw-8, j = gw-1; j >= 0; j--, x++)
				canvas[i * md.w + j] = (byte >> x) & 1 ? 0xff : 0;
		}
		write(fd, canvas, md.h * md.w);
	}
	free(canvas);
	fclose(fp);
	close(fd);
	free(gl);
	return 0;
}
