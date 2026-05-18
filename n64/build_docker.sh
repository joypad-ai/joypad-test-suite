#!/usr/bin/env bash
# One-shot Docker-based build for the N64 joypad-tester ROM. Pulls the
# LibDragon toolchain image and runs `make` inside it -- no local
# install of the mips64-elf cross-compiler needed.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-n64:latest"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building N64 toolchain image $IMAGE_TAG (one-time, pulls libdragon base)..."
            docker build --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/work" \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            make
        # Mirror the .z64 into build/ post-make so artifact paths match
        # the other consoles. Done outside make to avoid the %.z64
        # pattern from n64.mk applying target-specific LDFLAGS twice.
        cp -f joypad-tester.z64 build/joypad-tester.z64
        echo "Built build/joypad-tester.z64"
        ;;
    clean)
        rm -rf build/
        ;;
    rebuild-image)
        docker build --no-cache --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        ;;
    *)
        echo "Usage: $0 [build|clean|rebuild-image]" >&2
        exit 1
        ;;
esac
