# KiCad-ADK-MOD

The fork of KiCad used by the IHP Open ADK, the design flow for heterogeneous
chiplet integration on IHP SG13G2 dies and the IntM4TM2 silicon interposer. It
adds nanometre geometry, micrometre units, a HyperLynx exporter that keeps
nanometre precision and links footprints to their GDS files, and headless
`.hyp` and `.chiplet` exporters, which the Chiplets-KiCad-Plugin calls.
[`NANOMETER_MODIFICATIONS.md`](../NANOMETER_MODIFICATIONS.md) describes each
change and [`ci/fork-delta.txt`](../ci/fork-delta.txt) lists every file the fork
touches; everything else is upstream KiCad, whose README is
[`README.md`](../README.md).

## Use

Build the tree in a container, then start pcbnew from that build in the
board's directory (it needs an X display):

```bash
docker build -t kicad-builder . && mkdir -p build/release && docker run --rm -v "$PWD":/kicad-source kicad-builder
DESIGN_DIR=/path/to/board ./run_kicad_from_docker.sh
```

## Documentation

The full documentation is at
https://ihp-open-adk-docs.readthedocs.io/en/latest/tools/01_kicad_adk_mod.html

## Release

Released as part of the IHP Open ADK. The release page lists the commit of every
repository in each release:
https://ihp-open-adk-docs.readthedocs.io/en/latest/install/00_release.html

## License

GPL-3.0-or-later, as upstream KiCad. See `LICENSE`; `LICENSE.README` lists the
licences of the parts KiCad bundles.
