# trapexit/3do-devkit toolchain image for building Joypad Tester 3DO ROMs.
#
# The devkit ships pre-built ARM compilers + 3DO tools (3doiso, 3DOEncrypt,
# etc.) for Linux only. On Apple Silicon / non-x86 hosts the i386 binaries
# run via Docker's amd64 platform emulation. The two extra ingredients
# (libc6-i386 for the 32-bit dynamic loader, and wine32 for the Windows-
# binary 3doiso tool) come from Debian bookworm-backports.
FROM --platform=linux/amd64 debian:bookworm-slim

ARG DEVKIT_REPO=https://github.com/trapexit/3do-devkit.git
ARG DEVKIT_REF=e0845bc48ccb1a810d94fc0da20445eaf4a1846d

RUN dpkg --add-architecture i386 \
 && apt-get update -qq \
 && apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        make \
        libc6-i386 \
        wine \
        wine32 \
 && rm -rf /var/lib/apt/lists/*

# Clone the devkit at the pinned commit. Strip the example subdirs --
# our build replaces src/main.cpp with the joypad-tester source and
# only needs the root LaunchMe (no point shipping cel_rotation etc.).
RUN git clone "$DEVKIT_REPO" /opt/3do-devkit \
 && cd /opt/3do-devkit \
 && git checkout "$DEVKIT_REF" \
 && rm -rf src/*/ \
 && rm -rf .git docs art \
 # build_docker.sh runs the container as the host user via -u so any
 # output files land owned by that user; give the devkit tree
 # everyone-write so the host-uid can overlay src/main.cpp + write
 # build/ artefacts back.
 && chmod -R a+w /opt/3do-devkit

# Wine wants a writable HOME for its .wine prefix. The container runs
# as the host uid (so output files come back owned by the user), but
# that uid has no /home/<name>; create an everyone-writable home dir.
RUN mkdir -p /home/builder && chmod 777 /home/builder

ENV TDO_DEVKIT_PATH=/opt/3do-devkit/
ENV PATH=/opt/3do-devkit/bin/compiler/linux:/opt/3do-devkit/bin/tools/linux:/opt/3do-devkit/bin/buildtools/linux:$PATH

WORKDIR /work
