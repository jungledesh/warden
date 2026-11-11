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

WORKDIR /work/targets/toy
COPY targets/toy/toy.c .

COPY targets/lua/fetch.sh targets/lua/build.sh targets/lua/run_suite.sh /work/targets/lua/
RUN chmod +x /work/targets/lua/fetch.sh /work/targets/lua/build.sh /work/targets/lua/run_suite.sh

CMD ["bash"]