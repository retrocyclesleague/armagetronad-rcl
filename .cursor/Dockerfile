# RCL agent dev image — sty+ct+ap dedicated server build (no GL client).
# Build context is .cursor/ only; Cursor checks out the repo at runtime.

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    bash \
    bison \
    bzip2 \
    ca-certificates \
    curl \
    g++ \
    gdb \
    git \
    libtool \
    libxml2-dev \
    libzthread-dev \
    make \
    pkg-config \
    python3 \
    time \
    wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
