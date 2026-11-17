FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y clang-18 libclang-rt-18-dev \
    make curl ca-certificates time libreadline-dev \
 && rm -rf /var/lib/apt/lists/*

# Stable path to the preloadable Scudo library.
# The build fails here if the file isn't found for this CPU architecture.
RUN SO="$(clang-18 -print-resource-dir)/lib/linux/libclang_rt.scudo_standalone-$(uname -m).so" \
 && test -e "$SO" \
 && ln -s "$SO" /usr/local/lib/libscudo.so

ENV SCUDO_SO=/usr/local/lib/libscudo.so

# The side-file script calls llvm-nm, llvm-readelf, and llvm-objdump with no version.
RUN apt-get update \
 && apt-get install -y llvm-18 \
 && ln -s /usr/bin/llvm-nm-18 /usr/local/bin/llvm-nm \
 && ln -s /usr/bin/llvm-readelf-18 /usr/local/bin/llvm-readelf \
 && ln -s /usr/bin/llvm-objdump-18 /usr/local/bin/llvm-objdump \
 && rm -rf /var/lib/apt/lists/*

# curl is the C sample. Left unstripped so the side-file script can read it.
ARG CURL_VERSION=8.23.0
ARG CURL_SHA256=e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
RUN curl -fsSL -o /tmp/curl.tar.gz "https://github.com/curl/curl/releases/download/curl-8_23_0/curl-${CURL_VERSION}.tar.gz" \
 && echo "${CURL_SHA256}  /tmp/curl.tar.gz" | sha256sum -c - \
 && mkdir -p /tmp/curl-src \
 && tar -xzf /tmp/curl.tar.gz -C /tmp/curl-src --strip-components=1 \
 && cd /tmp/curl-src \
 && ./configure --disable-shared --without-ssl --without-zlib --without-libpsl \
      --without-nghttp2 --without-libidn2 --without-brotli --without-zstd \
      --disable-ldap --disable-manual \
      CC=clang-18 CFLAGS="-O0 -g" \
 && make -j"$(nproc)" \
 && make install \
 && rm -rf /tmp/curl-src /tmp/curl.tar.gz

WORKDIR /work

COPY scripts/sidefile /usr/local/bin/sidefile
COPY scripts/stamp /usr/local/bin/stamp
COPY src/warden /usr/local/src/warden
RUN chmod +x /usr/local/bin/sidefile /usr/local/bin/stamp \
 && clang-18 -std=c11 -O2 -Wall -Wextra -Werror -Wconversion \
      -I/usr/local/src/warden \
      -o /usr/local/bin/warden-stamp /usr/local/src/warden/stamp.c \
 && clang-18 -c /usr/local/src/warden/slot.S -o /usr/local/src/warden/slot.o \
 && ln -s /usr/bin/llvm-strip-18 /usr/local/bin/llvm-strip \
 && printf '%s\n' \
      'void *malloc(unsigned long);' \
      'void alloc(void) { malloc(16); }' \
      'int main(void) { alloc(); return 0; }' > /tmp/slotdemo.c \
 && clang-18 -O0 -g -Wl,--build-id \
      -Wl,-T,/usr/local/src/warden/slot.ld \
      /usr/local/src/warden/slot.o /tmp/slotdemo.c \
      -o /usr/local/bin/slotdemo \
 && rm -f /tmp/slotdemo.c

# curl again, this time linked with the empty hash slot. Left unstripped.
# /usr/bin/curl is Ubuntu's curl. The one we installed has no HTTPS.
RUN /usr/bin/curl -fsSL -o /tmp/curl.tar.gz "https://github.com/curl/curl/releases/download/curl-8_23_0/curl-${CURL_VERSION}.tar.gz" \
 && echo "${CURL_SHA256}  /tmp/curl.tar.gz" | sha256sum -c - \
 && mkdir -p /tmp/curl-src \
 && tar -xzf /tmp/curl.tar.gz -C /tmp/curl-src --strip-components=1 \
 && cd /tmp/curl-src \
 && ./configure --disable-shared --without-ssl --without-zlib --without-libpsl \
      --without-nghttp2 --without-libidn2 --without-brotli --without-zstd \
      --disable-ldap --disable-manual \
      CC=clang-18 CFLAGS="-O0 -g" \
      LDFLAGS="-Wl,--build-id -Wl,-T,/usr/local/src/warden/slot.ld" \
      LIBS="/usr/local/src/warden/slot.o" \
 && make -j"$(nproc)" \
 && make install \
 && rm -rf /tmp/curl-src /tmp/curl.tar.gz

# fmt is the C++ sample. One program linked with the empty hash slot. Left unstripped.
ARG FMT_VERSION=11.2.0
ARG FMT_SHA256=203eb4e8aa0d746c62d8f903df58e0419e3751591bb53ff971096eaa0ebd4ec3
RUN apt-get update \
 && apt-get install -y unzip g++ \
 && rm -rf /var/lib/apt/lists/* \
 && /usr/bin/curl -fsSL -o /tmp/fmt.zip "https://github.com/fmtlib/fmt/releases/download/${FMT_VERSION}/fmt-${FMT_VERSION}.zip" \
 && echo "${FMT_SHA256}  /tmp/fmt.zip" | sha256sum -c - \
 && mkdir -p /tmp/fmt-src \
 && unzip -q /tmp/fmt.zip -d /tmp \
 && mv /tmp/fmt-${FMT_VERSION}/* /tmp/fmt-src/ \
 && printf '%s\n' \
      '#include <fmt/core.h>' \
      'int main() {' \
      '  fmt::print("{}\n", fmt::format("{}", 1));' \
      '  return 0;' \
      '}' > /tmp/fmt-demo.cpp \
 && clang++-18 -std=c++17 -O0 -g -I/tmp/fmt-src/include \
      /tmp/fmt-src/src/format.cc /tmp/fmt-demo.cpp \
      /usr/local/src/warden/slot.o \
      -Wl,--build-id -Wl,-T,/usr/local/src/warden/slot.ld \
      -o /usr/local/bin/fmt-demo \
 && rm -rf /tmp/fmt-src /tmp/fmt.zip /tmp/fmt-demo.cpp /tmp/fmt-${FMT_VERSION}

CMD ["bash"]
