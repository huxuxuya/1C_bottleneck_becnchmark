FROM debian:12-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        binutils \
        build-essential \
        ca-certificates \
        cmake \
        file \
        g++-aarch64-linux-gnu \
        g++-i686-linux-gnu \
        g++-mingw-w64-i686 \
        g++-mingw-w64-x86-64 \
        g++-x86-64-linux-gnu \
        uuid-dev \
        ninja-build \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["/bin/bash"]
