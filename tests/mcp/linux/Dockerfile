FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake ninja-build g++ libboost-system-dev libssl-dev python3 file desktop-file-utils \
    && rm -rf /var/lib/apt/lists/*
