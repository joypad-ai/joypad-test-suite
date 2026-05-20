#!/usr/bin/env bash
#
# collect.sh — gather locally-built ROMs into one gitignored
# releases/ folder so the latest build of any console is easy to find
# regardless of which subdir you're working in.
#
#   ./collect.sh             # collect every console that has a build
#   ./collect.sh n64 dc      # collect only the named console(s)
#
# Each console's build_docker.sh calls `./collect.sh <console>` at the
# end of a successful build, so artifacts land here automatically; run
# it by hand any time to re-gather everything currently built.
#
# Names mirror the release-artifact scheme minus the version
# (joypad_tester_<console>.<ext>) -- versionless so the path is stable
# and you always grab the freshest build. The release workflow attaches
# the versioned names; this is a dev convenience only.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$ROOT/releases"
mkdir -p "$DEST"

want=("$@")
selected() {
    [ ${#want[@]} -eq 0 ] && return 0
    for w in "${want[@]}"; do [ "$w" = "$1" ] && return 0; done
    return 1
}

# copy <dest-name> <src-candidate>...  -- copies the first existing
# candidate to releases/<dest-name>. Missing sources are skipped
# silently (a console simply hasn't been built yet).
copy() {
    local name="$1"; shift
    local src
    for src in "$@"; do
        if [ -f "$src" ]; then
            cp -f "$src" "$DEST/$name"
            echo "  releases/$name  <-  ${src#"$ROOT"/}"
            return 0
        fi
    done
    return 0
}

selected gcn  && { copy joypad_tester_gcn.dol  "$ROOT"/gcn/joypad-tester-gamecube.dol "$ROOT"/gcn/build*/*gamecube*.dol
                   copy joypad_tester_wii.dol  "$ROOT"/gcn/joypad-tester-wii.dol      "$ROOT"/gcn/build*/*wii*.dol; }
selected gba  && copy joypad_tester_gba.gba  "$ROOT"/gba/build/tester/tester_mb.gba
selected pce  && copy joypad_tester_pce.pce  "$ROOT"/pce/build/joypad-tester.pce
selected 3do  && copy joypad_tester_3do.iso  "$ROOT"/3do/build/joypad-tester.iso
selected dc   && copy joypad_tester_dc.cdi   "$ROOT"/dc/build/joypad-tester-dreamcast.cdi
selected n64  && copy joypad_tester_n64.z64  "$ROOT"/n64/build/joypad-tester.z64
selected nuon && { copy joypad_tester_nuon.iso "$ROOT"/nuon/build/joypad-tester.iso
                   copy joypad_tester_nuon.run "$ROOT"/nuon/build/nuon.run; }

echo "Collected into releases/"
