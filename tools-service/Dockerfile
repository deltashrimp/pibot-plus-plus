# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# Builds oatpp from source (not packaged for Debian bullseye) and compiles the
# tools service.
FROM debian:bullseye AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libssl-dev \
        libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

ARG OATPP_VERSION=1.3.0
WORKDIR /tmp
RUN git clone --depth 1 --branch ${OATPP_VERSION} https://github.com/oatpp/oatpp.git \
    && cmake -S oatpp -B oatpp/build -DCMAKE_BUILD_TYPE=Release \
        -DOATPP_BUILD_TESTS=OFF \
    && cmake --build oatpp/build -j"$(nproc)" \
    && cmake --install oatpp/build

WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release . \
    && cmake --build build -j"$(nproc)"

# ---- Stage 2: runtime ----
FROM debian:bullseye AS runtime

# git (clone), zip (archive), curl (healthcheck) and ca-certificates (HTTPS
# clones). The binary links oatpp statically; only spdlog and fmt are shared.
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        git \
        zip \
        ca-certificates \
        libspdlog1 \
        libfmt7 \
        libatomic1 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /data

COPY --from=builder /app/build/pibot-tools /usr/local/bin/pibot-tools
COPY --from=builder /usr/lib/x86_64-linux-gnu/libspdlog.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libfmt.so* /usr/lib/x86_64-linux-gnu/
RUN ldconfig

# tools service configuration (values injected at runtime via docker-compose)
ENV TOOLS_API_KEY= \
    TOOLS_PORT=8084 \
    GCLONE_WORKDIR=/data \
    GCLONE_MAX_BYTES=20971520 \
    GCLONE_TTL_SECONDS=300 \
    LOG_LEVEL=info

EXPOSE 8084

HEALTHCHECK CMD curl -f http://localhost:8084/health || exit 1

ENTRYPOINT ["/usr/local/bin/pibot-tools"]
