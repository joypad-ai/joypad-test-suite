# HuC toolchain for building Joypad Tester PC Engine ROMs.
#
# Builds uli/huc (an enhanced HuC fork) at a pinned commit so CI is
# reproducible. The upstream toolchain hasn't moved since 2018; pinning
# avoids surprises if the repo ever changes.
FROM debian:bookworm-slim

ARG HUC_REPO=https://github.com/uli/huc.git
ARG HUC_REF=52a556a0556abb67a12889ca7047366a8cb2f59e

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        make \
        gcc \
        libc6-dev \
 && rm -rf /var/lib/apt/lists/*

RUN git clone "$HUC_REPO" /opt/huc \
 && cd /opt/huc \
 && git checkout "$HUC_REF" \
 # uli/huc was last touched in 2018; modern GCC flags new warnings as
 # errors (e.g. -Wformat-overflow in function.c). Drop -Werror so the
 # build tolerates newer toolchains. Warnings still surface in logs.
 && sed -i 's/-Werror //g' src/Make_src.inc \
 # Skip mkit/develo (PC-side serial bootloader for vintage Develo
 # hardware; needs x86-only sys/io.h) and src/{isolink,tools} (CD-ISO
 # linker + mml/mod2mml/pcxtool — the audio/graphics tools trip a
 # multiple-definition link error under modern GCC's -fno-common
 # default). We only need huc + pceas (mkit/as) to build HuCard ROMs
 # from C.
 && sed -i 's/^SUBDIRS = as develo$/SUBDIRS = as/' src/mkit/Makefile \
 && sed -i 's/^SUBDIRS = huc mkit isolink tools$/SUBDIRS = huc mkit/' src/Makefile \
 # Skip tgemu (an SDL-based PCE emulator we don't need to build the
 # ROM) and examples (built only as a smoke test). All we need is src.
 && sed -i 's/^SUBDIRS = src tgemu examples$/SUBDIRS = src/' Makefile \
 && make \
 && make install \
 && rm -rf /opt/huc/src /opt/huc/tgemu /opt/huc/.git

WORKDIR /workspace
