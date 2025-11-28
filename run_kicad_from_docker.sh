#!/bin/bash

# Grant X11 permission
xhost +local:docker

# Define directories
KICAD_SOURCE="${HOME}/git/heterogenic_chip_design_project/kicad"
DESIGN_DIR="${HOME}/git/heterogenic_chip_design_project/kicad_designs/interposer_example"
# This parent directory includes the library folder and other GDS tools
GDS_PROJECT_DIR="${HOME}/git/heterogenic_chip_design_project/gds_to_kicad_footprint"

echo "Starting KiCad (PCBNew) from Docker..."
echo "Mounting Source: $KICAD_SOURCE"
echo "Mounting Design: $DESIGN_DIR"
echo "Mounting GDS Project: $GDS_PROJECT_DIR"

docker run --rm \
  -e DISPLAY=$DISPLAY \
  -e KICAD_RUN_FROM_BUILD_DIR=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v "$KICAD_SOURCE":/kicad-source \
  -v "$DESIGN_DIR":"$DESIGN_DIR" \
  -v "$GDS_PROJECT_DIR":"$GDS_PROJECT_DIR" \
  kicad-builder \
  /kicad-source/build/release/pcbnew/pcbnew
