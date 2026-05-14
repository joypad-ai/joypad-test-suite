#!/usr/bin/env bash
# One-shot Docker-based build for the 3DO joypad-tester ROM. Builds
# the toolchain image from buildtools/Dockerfile on first run
# (clones trapexit/3do-devkit at a pinned commit), then runs make
# inside it with our src/ overlaid on the devkit's.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-3do:latest"
TARGET="joypad-tester"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building 3DO toolchain image $IMAGE_TAG (one-time)..."
            docker build --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build .wine-home
        # Locate portfolio_os source. Prefer ~/git/portfolio_os (a
        # developer-facing fork checkout where source edits land) so
        # changes to KeyboardDriver.c, SillyPadDriver alongside, etc.
        # flow through every build immediately. Fall back to a shallow
        # clone in .portfolio_os/ for fresh checkouts of this repo.
        if [ -d "$HOME/git/portfolio_os/.git" ]; then
            PORTFOLIO_OS_DIR="$HOME/git/portfolio_os"
            echo "[build_docker.sh] Using fork at $PORTFOLIO_OS_DIR"
        else
            PORTFOLIO_OS_DIR="$PWD/.portfolio_os"
            if [ ! -d "$PORTFOLIO_OS_DIR/.git" ]; then
                echo "[build_docker.sh] Cloning trapexit/portfolio_os (one-time, ~MB)..."
                git clone --depth 1 https://github.com/trapexit/portfolio_os.git "$PORTFOLIO_OS_DIR"
            fi
        fi

        # Host-side asset prep: invert logo_64.png so the silhouette
        # is white (renders opaque in an uncoded 16bpp cel) and the
        # background is black (renders as 0x0000 = transparent when
        # CCB_BGND is clear). 3it produces a broken all-zero PLUT
        # for our 1-bit grayscale source, so we sidestep PLUT-based
        # cels entirely and use uncoded instead.
        if [ ! -f assets/logo_64_inv.png ] || [ assets/logo_64.png -nt assets/logo_64_inv.png ]; then
            python3 - <<'PYEOF'
from PIL import Image, ImageOps
src = Image.open('assets/logo_64.png').convert('L')
inv = ImageOps.invert(src).convert('RGB')
inv.save('assets/logo_64_inv.png')
PYEOF
        fi

        # Run make inside the container. The image pre-baked /opt/3do-devkit
        # with empty src/<subdirs>/; we drop our main.cpp into the devkit's
        # src/, build, and copy the resulting iso back to our build/.
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/work" \
            -v "$PORTFOLIO_OS_DIR:/po:ro" \
            -u "$(id -u):$(id -g)" \
            -e HOME=/work/.wine-home \
            "$IMAGE_TAG" \
            bash -c "
                set -e

                # Asset pipeline (host-side art -> 3DO-format files
                # inside the devkit's takeme/ tree before the iso step
                # bundles them up):
                #   assets/banner.png   -> banner.png   (devkit Makefile
                #                                        runs 3it to-banner
                #                                        on this for the
                #                                        BIOS splash screen)
                #   assets/logo_64.png  -> takeme/LogoCel.cel (via 3it
                #                                              to-cel; main.cpp
                #                                              LoadCel's it)
                cp /work/assets/banner.png /opt/3do-devkit/banner.png
                # Bake the inverted logo as uncoded 16bpp with
                # CCB_BGND clear. With uncoded, each pixel IS the
                # colour: black source bg becomes 0x0000 (cel
                # engine treats as transparent when CCB_BGND is
                # clear), white silhouette stays 0x7FFF (opaque).
                # No PLUT involved, so 3it's broken-PLUT-for-1bit-
                # input behaviour doesn't matter, and no runtime
                # CCB / PLUT manipulation is needed. The inverted
                # PNG (silhouette=white, bg=black) is generated on
                # the host before this docker run, see invert
                # step in this script above.
                3it to-cel -b 16 --coded false --packed false \
                    --ccb-bgnd unset \
                    -o /opt/3do-devkit/takeme/LogoCel.cel \
                    /work/assets/logo_64_inv.png

                # Stage our source over the devkit's example launcher.
                cp /work/src/main.cpp /opt/3do-devkit/src/main.cpp

                # Build a replacement eventbroker daemon from the full
                # Portfolio OS source (trapexit/portfolio_os, snapshot
                # of the 1995 source release). The daemon shipped in
                # the devkit's takeme/System/Tasks/eventbroker is a
                # stripped placeholder that returns ER_NotSupported
                # (0xF14CD010) for advanced broker messages -- it
                # falls through to the 'default:' case for anything
                # past EB_Configure / EB_EventRecord. That kills
                # EB_DescribePods (and therefore non-pad device
                # enumeration). The full source in src/input/ has the
                # complete case statement set; we compile it here
                # with our matching armcc/armlink flags and drop the
                # output over the stripped binary.
                mkdir -p /tmp/eb_build && cd /tmp/eb_build
                PO=/po
                INCS=\"-I\$PO/src/input/includes -I\$PO/src/kernel/includes -I\$PO/src/filesystem/includes -I\$PO/src/includes\"
                CFLAGS=\"-bigend -za1 -zi4 -fa -fh -fx -fpu none -arch 3 -apcs 3/32/fp/swst/wide/softfp\"
                ASFLAGS=\"-bigend -fpu none -arch 3 -apcs 3/32/fp/swst\"

                # Stubs for two symbols clib.lib references but no
                # devkit lib defines (would have come from kernel.lib
                # which the trapexit devkit dropped). Neither is on
                # the broker hot path; we just need the linker
                # to resolve them.
                cat > stubs.c <<'STUBS_EOF'
char whatstring[64] = \"@(#)eventbroker joypad-tester\";
unsigned int SuperDiscOsVersion(unsigned int x) { (void)x; return 0x150A; }
STUBS_EOF
                armcc -DCONTROLPORT -DARMC -O \$CFLAGS \$INCS -c stubs.c -o stubs.do

                # Compile the cport driverlets (Stick, Mouse, Lightgun,
                # Glasses) directly into the broker as static drivers
                # rather than relying on the disc-load path through
                # DefaultDriver. Each one defines 'DriverletEntry' as
                # its entry point; we rename via -D per-file to avoid
                # symbol clashes, then register them in EventBroker.c
                # via AddStaticDriver. The driver-side interface
                # (PodInterface *, KernelBase *) gets adapted to the
                # broker-side (PodInterface *) by a thin wrapper.
                cp \$PO/src/input/EventBroker.c EventBroker.c

                # Patch EventBroker.c to declare extern entries for
                # each cport driver and register them via
                # AddStaticDriver. Static driver signature is
                #   Err entry(PodInterface *)
                # which matches Err DriverletEntry(PodInterface *,
                # KernelBase *) when invoked with the kbase arg
                # left undefined (the cport drivers never use kbase).
                #
                # The SillyPad / Arcade driver (0xC0) is our own
                # hand-rolled implementation -- Portfolio doesn't
                # ship a CPORTC0.ROM, and even Orbatak (the only
                # retail game using SillyPad) baked its driver into
                # its launchme binary rather than shipping a
                # separate cport. Registering 0xC0 here AFTER the
                # existing AddStaticDriver(0xC0, ControlPadDriver)
                # line is intentional: AddHead inserts at the list
                # head, so our SillyPad entry will be matched first
                # when the broker walks the list looking for a 0xC0
                # device.
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
};
                  }
                  if (/^extern Err ControlPadDriver/) {
                    \$_ .= qq{extern Err _Stick_DriverletEntry(PodInterface *);
extern Err _Keyboard_DriverletEntry(PodInterface *);
extern Err _Glasses_DriverletEntry(PodInterface *);
extern Err _Mouse_DriverletEntry(PodInterface *);
extern Err _Lightgun_DriverletEntry(PodInterface *);
extern Err _Sillypad_DriverletEntry(PodInterface *);
};
                  }
                ' EventBroker.c

                # Compile each cport driverlet with its entry symbol
                # renamed via -D to avoid clashes when linking them
                # all together.
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Stick_DriverletEntry    -O \$CFLAGS \$INCS -c \$PO/src/input/StickDriver.c   -o StickDriver.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Mouse_DriverletEntry    -O \$CFLAGS \$INCS -c \$PO/src/input/MouseDriver.c   -o MouseDriver.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Lightgun_DriverletEntry -O \$CFLAGS \$INCS -c \$PO/src/input/LightGunRom.c  -o LightGunRom.do
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Glasses_DriverletEntry  -O \$CFLAGS \$INCS -c \$PO/src/input/GlassesDriver.c -o GlassesDriver.do
                # Keyboard driver from portfolio_os is incomplete --
                # KeyboardDriver.c implements PD_InitDriver / PD_InitPod
                # (sets POD_IsKeyboard flag) and PD_ProcessCommand /
                # PD_ConstructPodOutput, but PD_ParsePodInput is a stub
                # (no key decoding) and PD_AppendEventFrames is wrapped
                # in #ifdef NOTDEF (compiled out). Linking it anyway
                # makes the broker properly initialize a keyboard pod
                # if one shows up on the bus -- pod_Flags will gain
                # POD_IsKeyboard so detection is clean even though no
                # keystroke events would fire.
                # KeyboardDriver uses the static pattern Err
                # KeyboardDriver(PodInterface *) -- same shape as
                # ControlPadDriver, single arg. So we rename via
                # -D on KeyboardDriver (not DriverletEntry).
                armcc -DCONTROLPORT -DARMC -DKeyboardDriver=_Keyboard_DriverletEntry -O \$CFLAGS \$INCS -c \$PO/src/input/KeyboardDriver.c -o KeyboardDriver.do
                # SillyPad / Arcade driver -- our hand-rolled source,
                # mounted in via /work from 3do/buildtools/.
                armcc -DCONTROLPORT -DARMC -DDriverletEntry=_Sillypad_DriverletEntry -O \$CFLAGS \$INCS -c /work/buildtools/SillyPadDriver.c -o SillyPadDriver.do

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
                  StickDriver.do MouseDriver.do LightGunRom.do GlassesDriver.do KeyboardDriver.do SillyPadDriver.do \\
                  DummyPutc.do autodocs.do RegisterGlue.do

                # modbin: set the daemon's task priority and stack
                # to match the original portfolio_os build settings
                # (eventbroker_MODBIN = -stack 1200 -pri 199).
                modbin --stack=1200 --pri=199 --name=eventbroker --time eventbroker eventbroker

                echo \"--- built eventbroker binary ---\"
                ls -la eventbroker
                # Drop it over the stripped binary the devkit ships.
                cp eventbroker /opt/3do-devkit/takeme/System/Tasks/eventbroker

                # Rename driverlet ROMs to uppercase so DefaultDriver
                # can find them. The broker source (DefaultDriver.c)
                # builds the path as 'sprintf(driverPath,
                # \"\$DRIVERS/CPORT%x.ROM\", pod->pod_Type)' which
                # produces CPORT1.ROM / CPORT41.ROM / CPORT49.ROM /
                # CPORT4D.ROM. The devkit ships them lowercase
                # (cport1.rom etc) and 3DO Opera-FS is case-sensitive,
                # so the loads silently fail and non-pad devices
                # never get a driverlet.
                cd /opt/3do-devkit/takeme/System/Drivers
                for f in cport*.rom; do
                  upper=\$(echo \"\$f\" | tr 'a-z' 'A-Z')
                  [ \"\$f\" = \"\$upper\" ] || mv \"\$f\" \"\$upper\"
                done
                ls

                cd /opt/3do-devkit
                # make NAME=... (not env-prefix) -- env vars are
                # shadowed by the Makefile's own 'NAME = helloworld'
                # assignment, but command-line overrides win.
                #
                # 'banner' is a phony target the default goal doesn't
                # depend on, so to actually swap the BannerScreen file
                # from the devkit's default ('FICTIONAL DEVELOPER /
                # BOGUS TITLE') to our own we have to invoke it
                # explicitly. Order matters: banner first so it lands
                # in takeme/ before the iso composer reads the tree.
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
