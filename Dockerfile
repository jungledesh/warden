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

# Valkey linked to libc malloc. The Linux default compiles in jemalloc,
# and LD_PRELOAD cannot replace that.
ARG VALKEY_VERSION=9.1.3
ARG VALKEY_SHA256=184e34ed2d9b51879b682b5b15205e2b9d677c60280e3cb453be8f520d04e293
RUN curl -fsSL -o /tmp/valkey.tar.gz "https://github.com/valkey-io/valkey/archive/refs/tags/${VALKEY_VERSION}.tar.gz" \
 && echo "${VALKEY_SHA256}  /tmp/valkey.tar.gz" | sha256sum -c - \
 && mkdir -p /tmp/valkey-src \
 && tar -xzf /tmp/valkey.tar.gz -C /tmp/valkey-src --strip-components=1 \
 && make -C /tmp/valkey-src -j"$(nproc)" MALLOC=libc CC=clang-18 \
 && make -C /tmp/valkey-src install PREFIX=/usr/local MALLOC=libc CC=clang-18 \
 && valkey-server --daemonize yes --bind 127.0.0.1 --port 6379 \
      --protected-mode yes --save "" --dir /tmp \
      --pidfile /tmp/valkey.pid --logfile /tmp/valkey.log \
 && i=0; while [ "$i" -lt 20 ]; do valkey-cli ping && break; i=$((i+1)); sleep 0.2; done \
 && valkey-cli INFO memory | grep -q '^mem_allocator:libc' \
 && valkey-cli SHUTDOWN NOSAVE \
 && rm -rf /tmp/valkey-src /tmp/valkey.tar.gz /tmp/valkey.pid /tmp/valkey.log

WORKDIR /work/targets/toy
COPY targets/toy/toy.c .

COPY targets/lua/fetch.sh targets/lua/build.sh targets/lua/run_suite.sh /work/targets/lua/
RUN chmod +x /work/targets/lua/fetch.sh /work/targets/lua/build.sh /work/targets/lua/run_suite.sh

CMD ["bash"]