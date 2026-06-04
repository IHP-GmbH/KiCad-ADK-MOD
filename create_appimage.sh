#!/bin/bash
#
# Package this KiCad fork as a distributable AppImage.
#
# Run inside the kicad-builder container with the repo mounted at
# /kicad-source and an existing release build at build/release:
#
#   docker run --rm -v "$PWD":/kicad-source kicad-builder \
#       bash /kicad-source/create_appimage.sh
#
# Knobs / notes:
#   - NINJA_JOBS  caps the install-time rebuild parallelism (default 4;
#                 link steps are RAM-bound).
#   - WXPY_SRC    dist-packages dir providing wxPython (default: the
#                 container's /usr/lib/python3/dist-packages). Bundling it
#                 is what keeps Python action plugins alive in the image.
#   - The output overwrites KiCad-x86_64.AppImage in the repo root. If a
#     KiCad instance is RUNNING from that file, appimagetool fails with
#     "Text file busy" -- close KiCad first.
#   - linuxdeploy + its gtk plugin are downloaded on first use and kept
#     next to this script (gitignored, never committed).
set -e

# 1. Setup directories
# Ensure we are in the root of the repo
cd /kicad-source

BUILD_DIR="build/release"
APPDIR="AppDir"

echo "Creating AppDir..."
rm -rf $APPDIR
mkdir -p $APPDIR

# 2. Install into AppDir
echo "Installing to AppDir..."
cd $BUILD_DIR
# KiCad defaults to /usr/local. We want it in /usr for the AppImage standard layout.
# So we use DESTDIR to capture it, then move it.
# NINJA_JOBS caps parallelism (RAM-bound link steps); default 4.
DESTDIR=../../$APPDIR ninja -j${NINJA_JOBS:-4} install
cd ../..

# 3. Move /usr/local to /usr inside AppDir
if [ -d "$APPDIR/usr/local" ]; then
    echo "Moving /usr/local to /usr..."
    mkdir -p $APPDIR/usr
    cp -r $APPDIR/usr/local/* $APPDIR/usr/
    rm -rf $APPDIR/usr/local
fi

# 3.1 Manual fix: Ensure resources (icons) are present
echo "Manually copying resources to ensure icons work..."
mkdir -p $APPDIR/usr/share/kicad/resources
cp $BUILD_DIR/resources/images.tar.gz $APPDIR/usr/share/kicad/resources/

# 3.2 Bundle wxPython next to the pcbnew bindings. KiCad's embedded Python
# needs `import wx` for action plugins ("Could not determine wxWidgets
# version. Python plugins will not be available." without it). The build
# container's python3-wxgtk4.0 is compiled against the exact wxWidgets the
# AppImage bundles, so ABI matches by construction. linuxdeploy runs after
# this and resolves/bundles the wx extension modules' library deps.
WXPY_SRC=${WXPY_SRC:-/usr/lib/python3/dist-packages}
if [ -d "$WXPY_SRC/wx" ]; then
    echo "Bundling wxPython from $WXPY_SRC..."
    mkdir -p $APPDIR/usr/lib/python3/dist-packages
    cp -r $WXPY_SRC/wx $APPDIR/usr/lib/python3/dist-packages/
else
    echo "WARNING: wxPython not found at $WXPY_SRC -- Python action plugins"
    echo "         will be unavailable in the produced AppImage."
fi

# 4. Download linuxdeploy and plugins
echo "Downloading linuxdeploy..."
if [ ! -f linuxdeploy-x86_64.AppImage ]; then
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi

echo "Downloading linuxdeploy GTK plugin..."
if [ ! -f linuxdeploy-plugin-gtk.sh ]; then
    wget https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
    chmod +x linuxdeploy-plugin-gtk.sh
fi

# 5. Run linuxdeploy
echo "Generating AppImage..."
# We need to set this to run linuxdeploy inside Docker without FUSE
export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_PLUGIN_GTK_DIR=$(pwd)

# Help linuxdeploy find the libraries we just installed into AppDir
export LD_LIBRARY_PATH=$(pwd)/$APPDIR/usr/lib:$LD_LIBRARY_PATH

# We target the main 'kicad' executable as the entry point
./linuxdeploy-x86_64.AppImage \
    --appdir $APPDIR \
    --plugin gtk \
    --output appimage \
    --desktop-file $APPDIR/usr/share/applications/org.kicad.kicad.desktop \
    --icon-file $APPDIR/usr/share/icons/hicolor/scalable/apps/kicad.svg \
    --executable $APPDIR/usr/bin/pcbnew \
    --executable $APPDIR/usr/bin/eeschema \
    --executable $APPDIR/usr/bin/gerbview \
    --executable $APPDIR/usr/bin/kicad-cli

echo "AppImage creation complete!"
ls -lh *.AppImage
