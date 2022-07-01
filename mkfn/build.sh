#!/bin/sh -e
# Generate fbpad fonts
CFLAGS="\
-pedantic -Wall -Wextra \
-Wno-implicit-fallthrough \
-Wno-missing-field-initializers \
-Wno-unused-parameter \
-Wfatal-errors -std=c99 \
-lm \
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
	[ -x mkfn_bdf ] || build
	run mkdir -p "$DESTDIR$PREFIX/bin/"
	run cp -f mkfn "$DESTDIR$PREFIX/bin/mkfn"
	run cp -f mkfn_bdf "$DESTDIR$PREFIX/bin/mkfn_bdf"
}

build() {
	run "$CC" "mkfn.c" $CFLAGS -o mkfn
	run "$CC" "mkfn_bdf.c" $CFLAGS -o mkfn_bdf
}

mkfn() {
	FP="/root/klec/cgi/terminus-ttf-4.47.0"
	OP="-h18 -w9"
	SZ="18h96v96"
	run ./mkfn $OP -o ../ar.tf $FP/TerminusTTF-4.47.0.ttf $SZ
	run ./mkfn $OP -o ../ai.tf $FP/TerminusTTF-Italic-4.47.0.ttf $SZ
	run ./mkfn $OP -o ../ab.tf $FP/TerminusTTF-Bold-4.47.0.ttf $SZ
}

mkfn_bdf() {
	FP="/root/klec/cgi"
	SZ="16"
	if [ "$1" ]; then
		SZ=$1		
	fi
	run ./mkfn_bdf $FP/ter-u"$SZ"n.bdf ../ar.tf
	run ./mkfn_bdf $FP/ter-u"$SZ"v.bdf ../ai.tf
	run ./mkfn_bdf $FP/ter-u"$SZ"b.bdf ../ab.tf
}

if [ "$#" -gt 0 ]; then
	"$@"
else
	build
fi
