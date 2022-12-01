#!/bin/sh

set -eux
PG_PROCS="${PG_PROCS:""}"

if [ -z "$PG_PROCS" ] && command -v nproc; then
  PG_PROCS="$(nproc)"
elif [ -z "$PG_PROCS" ]; then
  PG_PROCS="$(sysctl -n hw.logicalcpu)"
fi

ORIGINAL_DIR="$PWD"
cd "$(git rev-parse --show-toplevel)"

# BearSSL
cd vendor/bearssl/
make -j "$PG_PROCS"
mkdir -p build/install/lib  build/install/include
cp -R inc/ build/install/include
cp build/libbearssl.a build/install/lib
cd ../..

test -f vendor/bearssl/build/install/lib/libbearssl.a
test -f vendor/bearssl/build/install/include/bearssl.h

# Curl
cd vendor/curl
if ! [ -f ./configure ]; then
  autoreconf -fi
  ./configure \
    --disable-alt-svc \
    --disable-ares \
    --disable-dateparse \
    --disable-dict \
    --disable-dnsshuffle \
    --disable-doh \
    --disable-file \
    --disable-ftp \
    --disable-gopher \
    --disable-http-auth \
    --disable-imap \
    --disable-largefile \
    --disable-ldap \
    --disable-ldaps \
    --disable-ldaps \
    --disable-manual \
    --disable-mime \
    --disable-mqtt \
    --disable-netrc \
    --disable-ntlm \
    --disable-pop3 \
    --disable-progress-meter \
    --disable-proxy \
    --disable-rtsp \
    --disable-shared \
    --disable-smb \
    --disable-smtp \
    --disable-sspi \
    --disable-telnet \
    --disable-tftp \
    --disable-threaded-resolver \
    --disable-tls-srp \
    --disable-unix-sockets \
    --enable-http \
    --enable-optimize \
    --with-bearssl="$PWD"/../bearssl/build/install \
    --without-brotli \
    --without-hyper \
    --without-libgsasl \
    --without-libidn2 \
    --without-libpsl \
    --without-libpsl \
    --without-librtmp \
    --without-msh3 \
    --without-nghttp2 \
    --without-nghttp3 \
    --without-ngtcp2 \
    --without-quiche \
    --without-winidn \
    --without-zlib \
    --without-zstd
fi
test -f ./configure
make -j"$(PG_PROCS)"
cd ../..

test -f ./vendor/curl/lib/.libs/libcurl.a

cd "$ORIGINAL_DIR"
