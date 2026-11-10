# KiCad-ADK-MOD

The IHP fork of KiCad used by the IHP Open ADK, the design flow for
heterogeneous chiplet integration on IHP SG13G2 dies and the IntM4TM2 silicon
interposer. It carries a small delta on top of upstream KiCad for
interposer-scale boards: nanometre geometry limits and rendering, micrometre
units in the PCB tools, a HyperLynx exporter that keeps nanometre precision and
links footprints to their GDS files, and headless `.hyp` and `.chiplet`
exporters callable from Python.

[`NANOMETER_MODIFICATIONS.md`](../NANOMETER_MODIFICATIONS.md) describes each
change, and [`ci/fork-delta.txt`](../ci/fork-delta.txt) lists every file the
fork touches against upstream. Everything else is upstream KiCad, whose own
README is [`README.md`](../README.md).

## Getting it

    git clone https://github.com/IHP-GmbH/KiCad-ADK-MOD.git

It builds like upstream KiCad, with Python scripting on. The configuration the
ADK uses is in the ADK documentation,
<https://ihp-open-adk-docs.readthedocs.io>. The `Dockerfile` builds the same
tree in a container (see the top of `run_kicad_from_docker.sh`), and
`create_appimage.sh` packages a build as an AppImage.

A `.chiplet` is exported through the Chiplet Export pcbnew plugin,
<https://github.com/IHP-GmbH/Chiplets-KiCad-Plugin>, which calls the exporters
above.
