# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# Compiles the rp-service C++ service.
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
        libhiredis-dev \
        libredis++-dev \
    && rm -rf /var/lib/apt/lists/*

# Build the rp service
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
        libhiredis \
        libredis++ \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/pibot-rp /usr/local/bin/pibot-rp

# rp service configuration
ENV CORE_API_KEY= \
    POSTGRES_HOST=postgres \
    REDIS_HOST=redis \
    RP_PORT=8081

EXPOSE 8081

ENTRYPOINT ["/usr/local/bin/pibot-rp"]
