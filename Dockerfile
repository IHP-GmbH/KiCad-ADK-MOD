FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gettext \
    libglm-dev \
    zlib1g-dev \
    libzstd-dev \
    libcurl4-openssl-dev \
    libspnav-dev \
    libcairo2-dev \
    libpixman-1-dev \
    libgit2-dev \
    libboost-all-dev \
    libfreetype-dev \
    libharfbuzz-dev \
    libfontconfig1-dev \
    libngspice0-dev \
    libocct-data-exchange-dev \
    libocct-draw-dev \
    libocct-foundation-dev \
    libocct-modeling-algorithms-dev \
    libocct-modeling-data-dev \
    libocct-ocaf-dev \
    libocct-visualization-dev \
    protobuf-compiler \
    libprotobuf-dev \
    swig \
    python3 \
    python3-dev \
    python3-wxgtk4.0 \
    libwxgtk3.2-dev \
    libglew-dev \
    libgl1-mesa-dev \
    libnng-dev \
    unixodbc-dev \
    libgtk-3-dev \
    libsecret-1-dev \
    libpoppler-dev \
    libpoppler-glib-dev \
    libpoppler-cpp-dev \
    libwxgtk-webview3.2-dev \
    wget \
    file \
    appstream \
    desktop-file-utils \
    patchelf \
    doxygen \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /kicad-source

# The source code will be mounted here
VOLUME /kicad-source

# Create build directory
RUN mkdir -p build/release

# Default command to run the build
CMD ["bash", "-c", "cd build/release && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DKICAD_SCRIPTING_WXPYTHON=ON -DKICAD_USE_OCC=ON -DKICAD_SPICE=ON ../.. && ninja"]
