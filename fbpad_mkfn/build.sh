#!/bin/sh -e
# Generate fbpad fonts
CFLAGS="\
-pedantic -Wall -Wextra \
-Wno-implicit-fallthrough \
-Wno-missing-field-initializers \
-Wno-unused-parameter \
-Wfatal-errors -std=c99 \
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
	[ -x vi ] || build
	run mkdir -p "$DESTDIR$PREFIX/bin/"
	run cp -f vi "$DESTDIR$PREFIX/bin/mkfn"
}

build() {
	run "$CC" "mkfn.c" $CFLAGS -o mkfn
}

mkfn() {
	FP="/root/klec/cgi/terminus-ttf-4.47.0"
	OP="-h20 -w12"
	SZ="16.5h135v125r-3"
	./mkfn $OP $FP/TerminusTTF-4.47.0.ttf:$SZ		>../ar.tf
	./mkfn $OP $FP/TerminusTTF-Italic-4.47.0.ttf:$SZ	>../ai.tf
	./mkfn $OP $FP/TerminusTTF-Bold-4.47.0.ttf:$SZ	>../ab.tf
}

if [ "$#" -gt 0 ]; then
	"$@"
else
	build
fi
