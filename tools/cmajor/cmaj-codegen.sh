#!/usr/bin/env bash
# Generate dependency-free C++ from a Cmajor patch, running the prebuilt cmaj CLI
# inside an Ubuntu 22.04 container (the Linux cmaj build needs the 22.04 library
# generation; this dev box is 24.04). Dev-time only — the generated C++ is committed,
# so cmaj/Docker never enter CI or the shipping build.
#
# Usage: tools/cmajor/cmaj-codegen.sh <input.cmajor> <output.h>   (paths relative to repo root)
# Requires Docker. In a shell without the docker group, invoke via:
#   sg docker -c "tools/cmajor/cmaj-codegen.sh <in> <out>"
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CMAJ_DIR="${CMAJ_DIR:-$HOME/.local/cmaj/linux/x64}"
IN="$1"; OUT="$2"

if [[ ! -x "$CMAJ_DIR/cmaj" ]]; then
  echo "error: cmaj not found at $CMAJ_DIR/cmaj (set CMAJ_DIR)" >&2; exit 1
fi

HOST_UID="$(id -u)"; HOST_GID="$(id -g)"
docker run --rm \
  -v "$CMAJ_DIR":/cmaj:ro \
  -v "$REPO":/work -w /work \
  ubuntu:22.04 bash -c '
    set -e
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        libwebkit2gtk-4.0-37 libjavascriptcoregtk-4.0-18 >/dev/null
    export LD_LIBRARY_PATH=/cmaj
    /cmaj/cmaj --version
    /cmaj/cmaj generate --target=cpp --maxFramesPerBlock=32 --output="'"$OUT"'" "'"$IN"'"
    # cmaj runs as root in the container; hand the output back to the host user.
    chown '"$HOST_UID:$HOST_GID"' "'"$OUT"'"
  '
echo "generated: $OUT"
