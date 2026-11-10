#!/bin/bash
# Start pcbnew from a build of this tree inside the kicad-builder image.
#
# Build first, from this directory:
#   docker build -t kicad-builder .
#   mkdir -p build/release
#   docker run --rm -v "$PWD":/kicad-source kicad-builder
# (the bind mount hides the image's own build/release, so it has to exist in
# the checkout).
#
# Directories, each read from the environment:
#   KICAD_SOURCE     this checkout, holding build/release (default: the
#                    directory of this script)
#   DESIGN_DIR       the board's project directory (default: the current
#                    directory)
#   GDS_PROJECT_DIR  optional, a directory the board refers to by absolute
#                    path, such as footprint libraries and GDS files
# DESIGN_DIR and GDS_PROJECT_DIR are mounted at their own paths, so absolute
# paths stored in the board still resolve inside the container.
set -euo pipefail

# Docker reads a relative -v source as a volume name, not a path.
abspath() { (cd "$1" && pwd); }

KICAD_SOURCE="$(abspath "${KICAD_SOURCE:-$(dirname "${BASH_SOURCE[0]}")}")"
DESIGN_DIR="$(abspath "${DESIGN_DIR:-.}")"
GDS_PROJECT_DIR="${GDS_PROJECT_DIR:-}"
: "${DISPLAY:?is not set; pcbnew needs an X display}"

if [ ! -x "$KICAD_SOURCE/build/release/pcbnew/pcbnew" ]; then
    echo "no pcbnew build under $KICAD_SOURCE/build/release; build it first (see the top of this script)" >&2
    exit 1
fi

mounts=(-v "$KICAD_SOURCE":/kicad-source -v "$DESIGN_DIR":"$DESIGN_DIR")
if [ -n "$GDS_PROJECT_DIR" ]; then
    GDS_PROJECT_DIR="$(abspath "$GDS_PROJECT_DIR")"
    mounts+=(-v "$GDS_PROJECT_DIR":"$GDS_PROJECT_DIR")
fi

# Grant X11 permission for this run only
xhost +local:docker
trap 'xhost -local:docker' EXIT

echo "Starting KiCad (PCBNew) from Docker..."
echo "Mounting Source: $KICAD_SOURCE"
echo "Mounting Design: $DESIGN_DIR"
echo "Mounting GDS Project: ${GDS_PROJECT_DIR:-(none)}"

docker run --rm \
  -e DISPLAY="$DISPLAY" \
  -e KICAD_RUN_FROM_BUILD_DIR=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  "${mounts[@]}" \
  -w "$DESIGN_DIR" \
  kicad-builder \
  /kicad-source/build/release/pcbnew/pcbnew
