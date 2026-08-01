# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# oatpp, oatpp-postgresql, hiredis and redis-plus-plus are not available as
# Debian (bullseye) packages, so they are built from source here. hiredis is
# built from source as well because Debian's libhiredis-dev ships its CMake
# package under the wrong name ("Hiredis"), which find_package(hiredis)
# cannot locate.
FROM debian:bullseye AS builder

# Build and compile-time dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        pkg-config \
        libssl-dev \
        libpq-dev \
        postgresql-server-dev-13 \
        libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

ARG OATPP_VERSION=1.3.0
ARG OATPP_PQSQL_VERSION=1.3.0
ARG HIREDIS_VERSION=1.1.0
ARG REDIS_PLUS_PLUS_VERSION=1.3.8
WORKDIR /tmp

RUN git clone --depth 1 --branch ${OATPP_VERSION} https://github.com/oatpp/oatpp.git \
    && cmake -S oatpp -B oatpp/build -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DOATPP_BUILD_TESTS=OFF \
    && cmake --build oatpp/build -j"$(nproc)" \
    && cmake --install oatpp/build

RUN git clone --depth 1 --branch ${OATPP_PQSQL_VERSION} https://github.com/oatpp/oatpp-postgresql.git \
    && cmake -S oatpp-postgresql -B oatpp-postgresql/build -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DOATPP_BUILD_TESTS=OFF \
    && cmake --build oatpp-postgresql/build -j"$(nproc)" \
    && cmake --install oatpp-postgresql/build

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

# Build the rp service. Bare library names (oatpp, hiredis, redis++) are
# resolved via -L because Debian's link-time linker does not search
# /usr/local/lib by default; oatpp also installs into a versioned subdir.
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXE_LINKER_FLAGS="-L/usr/local/lib -L/usr/local/lib/oatpp-1.3.0" . \
    && cmake --build build -j"$(nproc)"

# ---- Stage 2: runtime ----
FROM debian:bullseye AS runtime

# Shared libraries needed at runtime plus curl for healthchecks
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        libssl1.1 \
        libpq5 \
        libspdlog1 \
        libfmt7 \
        libatomic1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/pibot-rp /usr/local/bin/pibot-rp

# Libraries built from source (installed under /usr/local by cmake --install).
# oatpp and its postgresql module install into a versioned subdirectory, so
# their .so files are copied flat into /usr/local/lib for ldconfig to index.
COPY --from=builder /usr/local/lib/oatpp-1.3.0/liboatpp.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/oatpp-1.3.0/liboatpp-postgresql.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libhiredis.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libredis++.so* /usr/local/lib/
RUN ldconfig

# rp service configuration
ENV CORE_API_KEY= \
    POSTGRES_HOST=postgres \
    REDIS_HOST=redis \
    RP_PORT=8081

EXPOSE 8081

ENTRYPOINT ["/usr/local/bin/pibot-rp"]
