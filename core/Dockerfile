# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# Builds TDLib from source and compiles the core C++ service.
FROM debian:bullseye AS builder

# Build and compile-time dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        libssl-dev \
        libpq-dev \
        libspdlog-dev \
        liboatpp-dev \
        liboatpp-pqsql-dev \
    && rm -rf /var/lib/apt/lists/*

# TDLib is not packaged for Debian, so build it from source
ARG TDLIB_VERSION=1.8.0
WORKDIR /tmp
RUN git clone --depth 1 --branch v${TDLIB_VERSION} https://github.com/tdlib/td.git \
    && cmake -S td -B td/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build td/build -j"$(nproc)" \
    && cmake --install td/build \
    && ldconfig

# Build the core service
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release . \
    && cmake --build build -j"$(nproc)"

# ---- Stage 2: runtime ----
FROM debian:bullseye AS runtime

# Shared libraries needed at runtime plus curl for healthchecks
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        libssl1.1 \
        libpq5 \
        liboatpp \
        liboatpp-pqsql \
        libtd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/pibot-core /usr/local/bin/pibot-core
COPY --from=builder /usr/local/lib/libtdjson* /usr/local/lib/

# Core service configuration
ENV CORE_API_KEY= \
    POSTGRES_HOST=postgres \
    POSTGRES_PORT=5432 \
    CORE_PORT=8080

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/pibot-core"]
