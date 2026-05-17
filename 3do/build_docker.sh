#!/usr/bin/env bash
# Docker-based build for the 3DO joypad-tester ROM.
#
# Compiles main.cpp against trapexit/3do-devkit and links a custom
# eventbroker daemon (full source from trapexit/portfolio_os, plus a
# SillyPadDriver for 0xC0) into the ISO. Driverlets are static-linked
# into the broker, not disc-loaded.

set -euo pipefail
cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-3do:latest"
TARGET="joypad-tester"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            docker build --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build .wine-home

        # Use a local portfolio_os fork if present so source edits flow
        # through every build; otherwise shallow-clone upstream.
        if [ -d "$HOME/git/portfolio_os/.git" ]; then
            PORTFOLIO_OS_DIR="$HOME/git/portfolio_os"
        else
            PORTFOLIO_OS_DIR="$PWD/.portfolio_os"
            [ -d "$PORTFOLIO_OS_DIR/.git" ] || \
                git clone --depth 1 https://github.com/trapexit/portfolio_os.git "$PORTFOLIO_OS_DIR"
        fi

        # The cel format we use (uncoded 16bpp with CCB_BGND clear)
        # needs the silhouette pixels nonzero and the background pixels
        # 0x0000 -- so invert the source before 3it sees it.
        if [ ! -f assets/logo_64_inv.png ] || [ assets/logo_64.png -nt assets/logo_64_inv.png ]; then
            python3 - <<'PYEOF'
from PIL import Image, ImageOps
src = Image.open('assets/logo_64.png').convert('L')
ImageOps.invert(src).convert('RGB').save('assets/logo_64_inv.png')
PYEOF
        fi

        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/work" \
            -v "$PORTFOLIO_OS_DIR:/po:ro" \
            -u "$(id -u):$(id -g)" \
            -e HOME=/work/.wine-home \
            "$IMAGE_TAG" \
            bash -c "
                set -e

                cp /work/assets/banner.png /opt/3do-devkit/banner.png
                3it to-cel -b 16 --coded false --packed false \
                    --ccb-bgnd unset \
                    -o /opt/3do-devkit/takeme/LogoCel.cel \
                    /work/assets/logo_64_inv.png
                cp /work/src/main.cpp /opt/3do-devkit/src/main.cpp

                # ---- rebuild eventbroker ----
                # The daemon shipped in the devkit's takeme/ is stripped
                # and returns ER_NotSupported for everything past
                # EB_Configure / EB_EventRecord, which kills non-pad
                # device enumeration. We rebuild from the full
                # portfolio_os source and static-link the driverlets.
                mkdir -p /tmp/eb_build && cd /tmp/eb_build
                PO=/po
                INCS=\"-I\$PO/src/input/includes -I\$PO/src/kernel/includes -I\$PO/src/filesystem/includes -I\$PO/src/includes\"
                CFLAGS=\"-bigend -za1 -zi4 -fa -fh -fx -fpu none -arch 3 -apcs 3/32/fp/swst/wide/softfp\"
                ASFLAGS=\"-bigend -fpu none -arch 3 -apcs 3/32/fp/swst\"

                # Stubs for symbols clib.lib references but no devkit
                # lib defines (kernel.lib isn't in the trapexit devkit).
                cat > stubs.c <<'STUBS_EOF'
char whatstring[64] = \"@(#)eventbroker joypad-tester\";
unsigned int SuperDiscOsVersion(unsigned int x) { (void)x; return 0x150A; }
STUBS_EOF
                armcc -DCONTROLPORT -DARMC -O \$CFLAGS \$INCS -c stubs.c -o stubs.do

                # Static-link each driverlet. They all define
                # 'DriverletEntry' so we rename via -D per file to
                # avoid symbol clashes, then register each with
                # AddStaticDriver in EventBroker.c.
                cp \$PO/src/input/EventBroker.c EventBroker.c
                perl -i -pe '
                  if (/AddStaticDriver\\(0xC0, ControlPadDriver\\);/) {
                    \$_ .= qq{
                      AddStaticDriver(0x01, (Err (*)())_Stick_DriverletEntry);
                      AddStaticDriver(0x02, (Err (*)())_Keyboard_DriverletEntry);
                      AddStaticDriver(0x41, (Err (*)())_Glasses_DriverletEntry);
                      AddStaticDriver(0x49, (Err (*)())_Mouse_DriverletEntry);
                      AddStaticDriver(0x4B, (Err (*)())_Keyboard_DriverletEntry);
                      AddStaticDriver(0x4D, (Err (*)())_Lightgun_DriverletEntry);
                      AddStaticDriver(0xC0, (Err (*)())_Sillypad_DriverletEntry);
                      AddStaticDriver(0x0F, (Err (*)())_Wheel_DriverletEntry);
};
                  }
                  if (/^extern Err ControlPadDriver/) {
                    \$_ .= qq{extern Err _Stick_DriverletEntry(PodInterface *);
extern Err _Keyboard_DriverletEntry(PodInterface *);
extern Err _Glasses_DriverletEntry(PodInterface *);
extern Err _Mouse_DriverletEntry(PodInterface *);
extern Err _Lightgun_DriverletEntry(PodInterface *);
extern Err _Sillypad_DriverletEntry(PodInterface *);
extern Err _Wheel_DriverletEntry(PodInterface *);
};
                  }
                ' EventBroker.c

                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Stick_DriverletEntry    -O \$CFLAGS \$INCS -c \$PO/src/input/StickDriver.c   -o StickDriver.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Mouse_DriverletEntry    -O \$CFLAGS \$INCS -c \$PO/src/input/MouseDriver.c   -o MouseDriver.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Lightgun_DriverletEntry -O \$CFLAGS \$INCS -c \$PO/src/input/LightGunRom.c  -o LightGunRom.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Glasses_DriverletEntry  -O \$CFLAGS \$INCS -c \$PO/src/input/GlassesDriver.c -o GlassesDriver.do
                # KeyboardDriver uses 'Err KeyboardDriver(PodInterface *)' (static-pattern, not DriverletEntry).
                armcc -DCONTROLPORT -DARMC -DKeyboardDriver=_Keyboard_DriverletEntry -O \$CFLAGS \$INCS -c \$PO/src/input/KeyboardDriver.c -o KeyboardDriver.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Sillypad_DriverletEntry -O \$CFLAGS \$INCS -c /work/buildtools/SillyPadDriver.c -o SillyPadDriver.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Wheel_DriverletEntry    -O \$CFLAGS \$INCS -c /work/buildtools/WheelDriver.c    -o WheelDriver.do

                armcc -DCONTROLPORT -DARMC -O \$CFLAGS \$INCS -c EventBroker.c -o EventBroker.do
                for f in ControlPadDriver DefaultDriver SplitterDriver DummyPutc autodocs; do
                  armcc -DCONTROLPORT -DARMC -O \$CFLAGS \$INCS -c \$PO/src/input/\$f.c -o \$f.do
                done
                armasm \$ASFLAGS \$INCS \$PO/src/input/RegisterGlue.s -o RegisterGlue.do

                LIBPATH=/opt/3do-devkit/lib/3do
                LDFLAGS=\"-match 0x1 -nodebug -noscanlib -nozeropad -remove -aif -reloc -dupok -ro-base 0\"
                armlink -o eventbroker \$LDFLAGS \\
                  \$LIBPATH/threadstartup.o \$LIBPATH/copyright.o \\
                  stubs.do \\
                  \$LIBPATH/3dlib.lib \$LIBPATH/audio.lib \$LIBPATH/clib.lib \\
                  \$LIBPATH/codec.lib \$LIBPATH/compression.lib \$LIBPATH/cpluslib.lib \\
                  \$LIBPATH/exampleslib.lib \$LIBPATH/filesystem.lib \$LIBPATH/graphics.lib \\
                  \$LIBPATH/input.lib \$LIBPATH/international.lib \$LIBPATH/intmath.lib \\
                  \$LIBPATH/lib3do.lib \$LIBPATH/operamath.lib \$LIBPATH/string.lib \\
                  \$LIBPATH/swi.lib \\
                  EventBroker.do ControlPadDriver.do DefaultDriver.do SplitterDriver.do \\
                  StickDriver.do MouseDriver.do LightGunRom.do GlassesDriver.do KeyboardDriver.do SillyPadDriver.do WheelDriver.do \\
                  DummyPutc.do autodocs.do RegisterGlue.do

                # Match the original portfolio_os build settings
                # (eventbroker_MODBIN = -stack 1200 -pri 199).
                modbin --stack=1200 --pri=199 --name=eventbroker --time eventbroker eventbroker

                cp eventbroker /opt/3do-devkit/takeme/System/Tasks/eventbroker
                mkdir -p /work/build
                cp eventbroker /work/build/eventbroker

                # ---- compose ISO ----
                cd /opt/3do-devkit
                # 'banner' is phony; the default goal doesn't depend on
                # it. Invoke explicitly so our BannerScreen lands in
                # takeme/ before the iso composer reads the tree.
                make NAME=$TARGET banner
                make NAME=$TARGET
                cp iso/$TARGET.iso /work/build/$TARGET.iso
            "
        echo "Built build/$TARGET.iso"
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
