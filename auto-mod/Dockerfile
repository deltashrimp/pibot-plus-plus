# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
# Installs build dependencies, builds oatpp from source (not packaged for
# Debian bullseye), and compiles the auto-mod service.
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

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
    && rm -rf /var/lib/apt/lists/*

# The binary links oatpp statically (built from source above); only spdlog
# and fmt are shared. Copy those so the container is self-contained.
COPY --from=builder /app/build/pibot-auto-mod /usr/local/bin/pibot-auto-mod
COPY --from=builder /usr/lib/x86_64-linux-gnu/libspdlog.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libfmt.so* /usr/lib/x86_64-linux-gnu/
RUN ldconfig

ENV AUTO_MOD_PORT=8083 \
    LOG_LEVEL=info

EXPOSE 8083

HEALTHCHECK CMD curl -f http://localhost:8083/health || exit 1

ENTRYPOINT ["/usr/local/bin/pibot-auto-mod"]
