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
RUN chmod +x /usr/local/bin/sidefile

CMD ["bash"]
