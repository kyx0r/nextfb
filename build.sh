#!/bin/sh -e
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
	[ -x fbpad ] || build
	run mkdir -p "$DESTDIR$PREFIX/bin/"
	run cp -f fbpad "$DESTDIR$PREFIX/bin/fbpad"
}

build() {
	run "$CC" "fbpad.c" -DPWD=\"$(pwd)\" $CFLAGS -o fbpad
}

if [ "$#" -gt 0 ]; then
	"$@"
else
	build
fi
