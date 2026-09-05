# syntax=docker/dockerfile:1
FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171

RUN dpkg --add-architecture i386 \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        binutils \
        file \
        libc6:i386 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/build

CMD ["sleep", "infinity"]
