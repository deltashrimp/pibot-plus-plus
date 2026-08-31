# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# Installs build dependencies, builds TDLib from source (not packaged for
# Debian), and compiles the core service.
FROM debian:bullseye AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        gperf \
        pkg-config \
        libssl-dev \
        zlib1g-dev \
        libpq-dev \
        libpqxx-dev \
        libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

ARG TDLIB_VERSION=1.8.0
ARG OATPP_VERSION=1.3.0
WORKDIR /tmp
RUN git clone --depth 1 --branch v${TDLIB_VERSION} https://github.com/tdlib/td.git \
    && cmake -S td -B td/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build td/build -j"$(nproc)" \
    && cmake --install td/build \
    && ldconfig
RUN git clone --depth 1 --branch ${OATPP_VERSION} https://github.com/oatpp/oatpp.git \
    && cmake -S oatpp -B oatpp/build -DCMAKE_BUILD_TYPE=Release \
        -DOATPP_BUILD_TESTS=OFF \
    && cmake --build oatpp/build -j"$(nproc)" \
    && cmake --install oatpp/build

WORKDIR /app
COPY core/CMakeLists.txt ./core/CMakeLists.txt
COPY core/src/ ./core/src/
COPY common/ ./common/
WORKDIR /app/core
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release . \
    && cmake --build build -j"$(nproc)"

# ---- Stage 2: runtime ----
FROM debian:bullseye AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/core/build/pibot-core /usr/local/bin/pibot-core
COPY --from=builder /usr/local/lib/libtd* /usr/local/lib/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libpqxx* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libpq.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libssl.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libcrypto.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libspdlog.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libfmt.so* /usr/lib/x86_64-linux-gnu/
RUN ldconfig

# Core service configuration (values injected at runtime via docker-compose)
ENV TELEGRAM_TOKEN= \
    TELEGRAM_API_ID=0 \
    TELEGRAM_API_HASH= \
    POSTGRES_HOST=postgres \
    POSTGRES_PORT=5432 \
    POSTGRES_DB=pibot \
    POSTGRES_USER=pibot \
    POSTGRES_PASSWORD= \
    CORE_API_KEY= \
    CORE_PORT=8080 \
    LOG_LEVEL=info

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/pibot-core"]
