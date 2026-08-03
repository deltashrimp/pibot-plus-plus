# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# oatpp, hiredis and redis-plus-plus are not available as Debian (bullseye)
# packages, so they are built from source here. hiredis is built from source
# as well because Debian's libhiredis-dev ships its CMake package under the
# wrong name ("Hiredis"), which find_package(hiredis) cannot locate.
FROM debian:bullseye AS builder

# Build and compile-time dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        pkg-config \
        libssl-dev \
        libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

ARG OATPP_VERSION=1.3.0
ARG HIREDIS_VERSION=1.1.0
ARG REDIS_PLUS_PLUS_VERSION=1.3.8
WORKDIR /tmp

RUN git clone --depth 1 --branch ${OATPP_VERSION} https://github.com/oatpp/oatpp.git \
    && cmake -S oatpp -B oatpp/build -DCMAKE_BUILD_TYPE=Release \
        -DOATPP_BUILD_TESTS=OFF \
    && cmake --build oatpp/build -j"$(nproc)" \
    && cmake --install oatpp/build

RUN git clone --depth 1 --branch v${HIREDIS_VERSION} https://github.com/redis/hiredis.git \
    && cmake -S hiredis -B hiredis/build -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_SSL=OFF \
        -DDISABLE_TESTS=ON \
    && cmake --build hiredis/build -j"$(nproc)" \
    && cmake --install hiredis/build

RUN git clone --depth 1 --branch ${REDIS_PLUS_PLUS_VERSION} https://github.com/sewenew/redis-plus-plus.git \
    && cmake -S redis-plus-plus -B redis-plus-plus/build -DCMAKE_BUILD_TYPE=Release \
        -DREDIS_PLUS_PLUS_CXX_STANDARD=17 \
        -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
        -DREDIS_PLUS_PLUS_BUILD_ASYNC=OFF \
        -DREDIS_PLUS_PLUS_BUILD_STATIC=OFF \
    && cmake --build redis-plus-plus/build -j"$(nproc)" \
    && cmake --install redis-plus-plus/build

# Build the rp service. Dependencies (hiredis, redis++, oatpp) are installed
# under /usr/local by cmake --install; their imported CMake targets carry the
# include and library paths, so no extra -L flags are needed. oatpp is built
# static (like core), so no runtime .so is needed for it.
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY rp_phrases.json ./rp_phrases.json
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release . \
    && cmake --build build -j"$(nproc)"

# ---- Stage 2: runtime ----
FROM debian:bullseye AS runtime

# Shared libraries needed at runtime plus curl for healthchecks
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        libssl1.1 \
        libspdlog1 \
        libfmt7 \
        libatomic1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/pibot-rp /usr/local/bin/pibot-rp
COPY --from=builder /app/rp_phrases.json /app/rp_phrases.json

# Libraries built from source (installed under /usr/local by cmake --install).
COPY --from=builder /usr/local/lib/libhiredis.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libredis++.so* /usr/local/lib/
RUN ldconfig

# rp service configuration (values injected at runtime via docker-compose)
ENV REDIS_HOST=redis \
    REDIS_PORT=6379 \
    RP_API_KEY= \
    RP_PORT=8081 \
    RP_PHRASES_FILE=/app/rp_phrases.json \
    LOG_LEVEL=info

EXPOSE 8081

ENTRYPOINT ["/usr/local/bin/pibot-rp"]
