#!/bin/bash -xe

# packages for Debian
# dpkg --add-architecture i386 && apt-get update
# sudo apt-get install gcc-mingw-w64-i686 wine wine32 wine-binfmt imagemagick
#
# Arch:
# mingw-w64-gcc wine imagemagick

INSTALL=/tmp/unnethack_win32
DESTDIR=/tmp/unnethack_destdir
mkdir -p $DESTDIR $INSTALL

function compile_unnethack {
	env CFLAGS='-O2 -Wall -Wno-unused' ./configure \
		--host i686-w64-mingw32 \
		--prefix=$INSTALL \
		--with-owner="`id -un`" \
		--with-group="`id -gn`" \
		--build=i686-pc-mingw32 \
		--without-compression --disable-file-areas \
		--enable-score-on-botl --enable-realtime-on-botl \
		$GRAPHICS \
		&& make --trace install
}

rm -rf $INSTALL/share/unnethack $DESTDIR/unnethack-win32-*

GRAPHICS="--disable-mswin-graphics --enable-tty-graphics"
compile_unnethack
mv $INSTALL/share/unnethack/unnethack.exe $INSTALL/share/unnethack/UnNetHack.exe

GRAPHICS="--enable-mswin-graphics --disable-tty-graphics"
compile_unnethack
mv $INSTALL/share/unnethack/unnethack.exe $INSTALL/share/unnethack/UnNetHackW.exe

rm -f $INSTALL/share/unnethack/unnethack.exe.old

make win32_release
make release_archive
