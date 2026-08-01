# Syntax=docker/dockerfile:1

# ---- Stage 1: builder ----
FROM debian:bullseye AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
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

COPY --from=builder /app/build/pibot-auto-mod /usr/local/bin/pibot-auto-mod

ENV AUTO_MOD_PORT=8083

EXPOSE 8083

HEALTHCHECK CMD curl -f http://localhost:8083/health || exit 1

ENTRYPOINT ["/usr/local/bin/pibot-auto-mod"]
