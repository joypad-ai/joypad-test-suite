#!/usr/bin/env bash
# One-shot Docker-based build for users without a local HuC install.
# Builds the joypad-tester-huc image from buildtools/Dockerfile on first
# run (uli/huc pinned), then runs `make` inside it.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-huc:latest"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building HuC toolchain image $IMAGE_TAG (one-time)..."
            docker build -t "$IMAGE_TAG" buildtools/
        fi
        docker run --rm \
            -v "$PWD:/workspace" \
            -w /workspace \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            make
        "$(dirname "$0")/../collect.sh" pce || true
        ;;
    clean)
        docker run --rm \
            -v "$PWD:/workspace" \
            -w /workspace \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            make clean
        ;;
    rebuild-image)
        docker build --no-cache -t "$IMAGE_TAG" buildtools/
        ;;
    *)
        echo "Usage: $0 [build|clean|rebuild-image]" >&2
        exit 1
        ;;
esac
