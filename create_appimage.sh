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
set -euo pipefail

# 1. Setup directories
# Ensure we are in the root of the repo
cd /kicad-source

BUILD_DIR="build/release"
APPDIR="AppDir"

echo "Creating AppDir..."
rm -rf "${APPDIR:?}"
mkdir -p "$APPDIR"

# 2. Install into AppDir
echo "Installing to AppDir..."
cd $BUILD_DIR
# KiCad defaults to /usr/local. We want it in /usr for the AppImage standard layout.
# So we use DESTDIR to capture it, then move it.
# NINJA_JOBS caps parallelism (RAM-bound link steps); default 4.
DESTDIR="../../$APPDIR" ninja -j"${NINJA_JOBS:-4}" install
cd ../..

# 3. Move /usr/local to /usr inside AppDir
if [ -d "$APPDIR/usr/local" ]; then
    echo "Moving /usr/local to /usr..."
    mkdir -p "$APPDIR/usr"
    cp -r "$APPDIR"/usr/local/* "$APPDIR/usr/"
    rm -rf "${APPDIR:?}/usr/local"
fi

# 3.1 Manual fix: Ensure resources (icons) are present
echo "Manually copying resources to ensure icons work..."
mkdir -p "$APPDIR/usr/share/kicad/resources"
cp "$BUILD_DIR/resources/images.tar.gz" "$APPDIR/usr/share/kicad/resources/"

# 3.2 Bundle wxPython next to the pcbnew bindings. KiCad's embedded Python
# needs `import wx` for action plugins ("Could not determine wxWidgets
# version. Python plugins will not be available." without it). The build
# container's python3-wxgtk4.0 is compiled against the exact wxWidgets the
# AppImage bundles, so ABI matches by construction. linuxdeploy runs after
# this and resolves/bundles the wx extension modules' library deps.
WXPY_SRC=${WXPY_SRC:-/usr/lib/python3/dist-packages}
if [ -d "$WXPY_SRC/wx" ]; then
    echo "Bundling wxPython from $WXPY_SRC..."
    mkdir -p "$APPDIR/usr/lib/python3/dist-packages"
    cp -r "$WXPY_SRC/wx" "$APPDIR/usr/lib/python3/dist-packages/"
else
    # No wx means the produced AppImage cannot 'import wx', so every Python
    # action plugin -- including the plugin-driven Chiplet/Hyperlynx export --
    # is dead. Fail the build rather than ship a silently broken image.
    echo "ERROR: wxPython not found at $WXPY_SRC; the AppImage would have no" >&2
    echo "       'import wx' and no working Python action plugins." >&2
    echo "       Set ALLOW_NO_WXPYTHON=1 to build without it anyway." >&2
    [ "${ALLOW_NO_WXPYTHON:-0}" = "1" ] || exit 1
fi

# 4. Download linuxdeploy and plugins.
# Fetch to a temp file and rename only on success, so an interrupted download is
# never cached as a complete tool and then chmod +x'd and executed. The upstream
# URLs are unpinned (linuxdeploy ships only a rolling 'continuous' release, the
# gtk plugin only 'master'), so the bundle is not byte-reproducible across
# rebuilds; pin to a vetted sha256 here if reproducibility is required.
fetch_tool() {
    local url="$1" dest="$2"
    [ -f "$dest" ] && return 0
    echo "Downloading $dest..."
    wget -O "$dest.part" "$url"
    if [ ! -s "$dest.part" ]; then
        echo "ERROR: empty download from $url" >&2
        rm -f "$dest.part"
        exit 1
    fi
    mv "$dest.part" "$dest"
    chmod +x "$dest"
}

fetch_tool "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    linuxdeploy-x86_64.AppImage
fetch_tool "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
    linuxdeploy-plugin-gtk.sh

# 5. Run linuxdeploy
echo "Generating AppImage..."
# We need to set this to run linuxdeploy inside Docker without FUSE
export APPIMAGE_EXTRACT_AND_RUN=1
LINUXDEPLOY_PLUGIN_GTK_DIR="$(pwd)"
export LINUXDEPLOY_PLUGIN_GTK_DIR

# Help linuxdeploy find the libraries we just installed into AppDir. Guard the
# append so an unset LD_LIBRARY_PATH does not leave a trailing ':' -- an empty
# entry the loader reads as the current directory, which would let a stray .so
# in the source tree shadow a real library.
LD_LIBRARY_PATH="$(pwd)/$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

# We target the main 'kicad' executable as the entry point
./linuxdeploy-x86_64.AppImage \
    --appdir "$APPDIR" \
    --plugin gtk \
    --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/org.kicad.kicad.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/kicad.svg" \
    --executable "$APPDIR/usr/bin/pcbnew" \
    --executable "$APPDIR/usr/bin/eeschema" \
    --executable "$APPDIR/usr/bin/gerbview" \
    --executable "$APPDIR/usr/bin/kicad-cli"

echo "AppImage creation complete!"
ls -lh *.AppImage
