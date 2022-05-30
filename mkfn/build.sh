#!/bin/sh -e
# Generate fbpad fonts
CFLAGS="\
-pedantic -Wall -Wextra \
-Wno-implicit-fallthrough \
-Wno-missing-field-initializers \
-Wfatal-errors -std=c99 \
-lm -lfreetype \
-I /usr/include/freetype2 \
-D_POSIX_C_SOURCE=200809L $CFLAGS"

OS="$(uname)"
: ${CC:=$(command -v cc)}
: ${PREFIX:=/usr/local}
case "$OS" in *BSD*) CFLAGS="$CFLAGS -D_BSD_SOURCE" ;; esac

run() {
	printf '%s\n' "$*"
	"$@"
}

install() {
	[ -x mkfn ] || build
	run mkdir -p "$DESTDIR$PREFIX/bin/"
	run cp -f mkfn "$DESTDIR$PREFIX/bin/mkfn"
}

build() {
	run "$CC" "mkfn.c" $CFLAGS -o mkfn
}

mkfn() {
	FP="/root/klec/cgi/terminus-ttf-4.47.0"
	OP="-h12 -w6"
	SZ="9h96v96"
	./mkfn $OP $FP/TerminusTTF-4.47.0.ttf:$SZ		> ../ar.tf
	./mkfn $OP $FP/TerminusTTF-Italic-4.47.0.ttf:$SZ	> ../ai.tf
	./mkfn $OP $FP/TerminusTTF-Bold-4.47.0.ttf:$SZ		> ../ab.tf
}

if [ "$#" -gt 0 ]; then
	"$@"
else
	build
fi
