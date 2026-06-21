# Technical Documentation: Nanometer & Micrometer Support

This document records the ADK-specific modifications applied on top of upstream KiCad to support sub-micron features (down to 10nm), native Micrometer (µm) units in the UI, high-precision export, and an AppImage build. It is the only ADK-authored doc in this fork and is the primary change log versus upstream.

## 1. Internal Geometry Limits (10nm Support)

**Objective:** Lower the hardcoded safety limits that prevent creating features smaller than 0.01mm.

### Modified Files:
*   **`pcbnew/pcb_track.h`**
    *   **Symbol:** `GEOMETRY_MIN_SIZE`
    *   **Change:** `#define GEOMETRY_MIN_SIZE (int) ( 0.00001 * pcbIUScale.IU_PER_MM )` (10nm).
    *   **Implication:** Allows the router and DRC to accept track widths and via diameters down to 10nm.

*   **`include/board_design_settings.h`**
    *   **Symbols:** `MINIMUM_ERROR_SIZE_MM`, `MINIMUM_LINE_WIDTH_MM`
    *   **Change:** Both defaults set to `0.00001`.
    *   **Implication:** Board setup constraints now allow entering values as small as 10nm without validation errors.

*   **`pcbnew/dialogs/dialog_track_via_size.cpp`**
    *   **Symbol:** file-scope `const int minSize` used in the `DIALOG_TRACK_VIA_SIZE` constructor.
    *   **Change:** `const int minSize = (int) ( 0.00001 * pcbIUScale.IU_PER_MM );`. The value is passed into the member-init list for `m_trackWidth`, `m_viaDiameter`, and `m_viaDrill`, which clamp their inputs to that minimum. `TransferDataFromWindow` itself only runs `ValidateViaParameters` and stores values; it does not reference `minSize`.
    *   **Implication:** The "Track & Via Properties" dialog accepts values down to 10nm.

## 2. User Interface: Micrometer Units (µm)

**Objective:** Add "Micrometers" as a selectable display unit in the UI with sufficient precision.

### Modified Files:
*   **`common/eda_units.cpp`**
    *   **Function:** `EDA_UNIT_UTILS::UI::MessageTextFromValue`
    *   **Change:** The readability path now formats `EDA_UNITS::UM` with `%.4f` (long form) and `%.0f` (short form): `case EDA_UNITS::UM: format = short_form ? wxT( "%.0f" ) : wxT( "%.4f" );`.
    *   **Implication:** 1nm is displayed as `0.0010 µm` instead of `0.0 µm`.
    *   **Note:** The full-precision path `EDA_UNIT_UTILS::UI::StringFromValue` does not have a dedicated UM case; UM falls into `default: format = wxT( "%.10f" )`, so edit fields retain full nanometer precision.

*   **`common/tool/actions.cpp`, `include/tool/actions.h`**
    *   **Change:** Added the `micrometersUnits` action: `ACTIONS::micrometersUnits` is named `common.Control.micrometerUnits`, friendly name "Micrometers", `Parameter(EDA_UNITS::UM)`. Declared in `include/tool/actions.h`. The Go binding is wired in `common/tool/common_tools.cpp` (`SwitchUnits`).

*   **`pcbnew/toolbars_pcb_editor.cpp`**
    *   **Change:** Added the µm button to the left toolbar via `.AddAction( ACTIONS::micrometersUnits )`, alongside mm, inch, and mils.

*   **`pcbnew/toolbars_footprint_editor.cpp`, `pcbnew/toolbars_footprint_viewer.cpp`**
    *   **Change:** The same `.AddAction( ACTIONS::micrometersUnits )` button is added to the footprint editor and footprint viewer left toolbars, so um is selectable there too.

*   **`common/eda_draw_frame.cpp`**
    *   **Change:** Added the `case EDA_UNITS::UM: m_toolManager->RunAction( ACTIONS::micrometersUnits ); break;` arm so unit cycling and restore reach Micrometers.

## 3. Grid System

**Objective:** Provide usable grid presets for nanometer-scale design.

### Modified Files:
*   **`common/settings/app_settings.cpp`**
    *   **Function:** `APP_SETTINGS_BASE::DefaultGridSizeList`
    *   **Change:** Added five fine presets to the "else" block (used by Pcbnew):
        *   `0.005 mm` (5 µm)
        *   `0.001 mm` (1 µm)
        *   `0.0001 mm` (100 nm)
        *   `0.00001 mm` (10 nm)
        *   `0.000001 mm` (1 nm)
    *   **Implication:** These options appear in the grid dropdown menu. Note: KiCad internal units are integers (1nm resolution), so 1nm is the hard floor for grid snapping.

## 4. Export Formats: Hyperlynx

**Objective:** Ensure export files retain nanometer precision. The legacy Imperial/English export lost precision due to the floating-point conversion factor (0.0254).

### Modified Files:
*   **`pcbnew/exporters/export_hyperlynx.cpp`**
    *   **Function:** `HYPERLYNX_EXPORTER::generateHeaders`
        *   **Change:** Header changed from `{UNITS=ENGLISH LENGTH}` to `{UNITS=METRIC LENGTH}`.
    *   **Function:** `iu2hyp` (static helper)
        *   **Change:** Logic changed from the historical Imperial form `return iu / 1e9 / 0.0254;` to `return iu / ( PCB_IU_PER_MM * 1000.0 );` (export in meters).
    *   **Implication:**
        *   Exported files now use **meters** as the base unit.
        *   Example: a 200µm trace appears as `W=0.0002000000`.
        *   Precision loss from inch conversion is eliminated.

## 5. Rendering Fixes for Nanometer Scale (2025-11-27)

**Objective:** Fix visual artifacts where vias and tracks appeared with disproportionately large white borders/shadows at nanometer scales.

**Problem:** When working at nanometer scales (e.g. 10nm vias), some visual features (specifically the Locked Item Shadow and Hole Wall Painting) were using default millimeter-based sizes. For example, a shadow based on Silk Screen width (~0.15mm) would be 15,000 times larger than a 10nm via, appearing as a giant white halo. The locked-shadow default is `m_lockedShadowMargin = bds.GetLineThickness( F_SilkS ) * 4`.

### Modified Files:
*   **`pcbnew/pcb_painter.cpp`**
    *   **Function:** `PCB_PAINTER::draw( const PCB_VIA* aVia, int aLayer )`
        *   **Change (LAYER_LOCKED_ITEM_SHADOW):** Shadow width is clamped to `aVia->GetWidth( currentLayer ) / 4` when it exceeds the via width.
        *   **Change (LAYER_VIA_HOLEWALLS):** Hole wall thickness is clamped to `getViaDrillSize( aVia ) / 4.0` when it exceeds that maximum.
    *   **Function:** `PCB_PAINTER::draw( const PCB_TRACK* aTrack, int aLayer )`
        *   **Change (LAYER_LOCKED_ITEM_SHADOW):** Shadow width is clamped to the track width when it exceeds it.
    *   **Implication:** Visual artifacts are eliminated for nanometer-scale designs while maintaining normal appearance at standard PCB scales.

*   **`common/gal/cairo/cairo_gal.cpp`, `common/gal/cairo/cairo_compositor.cpp`**
    *   **Function:** `CAIRO_GAL::xform()` output, plus the compositor buffer sizing.
    *   **Change:** Output pixel coordinates are clamped with `std::clamp( rv.x, -CAIRO_COORD_LIMIT, CAIRO_COORD_LIMIT )` (and the compositor width/height floored with `std::max( ..., 1u )`).
    *   **Implication:** At nanometer-scale zoom, off-screen coordinates no longer exceed INT32 when pixman computes region extents, eliminating the "pixman_region32_init_rect: Invalid rectangle passed" spam.

---

## 6. Build & Deployment (AppImage)

**Objective:** Create a portable, standalone executable that runs on the host machine without root privileges or Docker GUI issues.

### Build System Changes:
*   **`Dockerfile`:**
    *   Base image set to `ubuntu:24.04`.
    *   Added dependencies for AppImage creation: `wget`, `file`, `appstream`, `desktop-file-utils`, `patchelf`.
*   **`common/paths.cpp`:**
    *   **Change:** `GetStockDataPath` checks for the `APPDIR` environment variable (`wxGetEnv( wxT( "APPDIR" ), &appDir )`).
    *   **Reason:** Enables the AppImage to find its own resources (icons, shared files) relative to itself, rather than looking in system paths (`/usr/share`).
*   **`create_appimage.sh`:**
    *   Custom script that bundles the compiled binaries, libraries, and resources into an AppImage using `linuxdeploy` (with the GTK plugin).
    *   Copies `resources/images.tar.gz` into the AppDir so icons are present.
    *   Sets `LD_LIBRARY_PATH` so the bundle finds its own internal libraries (`libkicommon`, etc.).

### Usage:
*   **Generation:** Run `./create_appimage.sh` inside the Docker container (with FUSE privileges).
*   **Execution:** `./KiCad-x86_64.AppImage` on the host machine.
*   **Libraries:** Standard libraries are cloned locally in `./libraries/` and must be added manually via the Preferences menu on first run.

## 7. HyperLynx Export: GDS Link (2025-11-28)

**Objective:** Allow users to associate a GDSII file path with a footprint in KiCad and have that path exported to the HyperLynx file for use in downstream flows.

### Modified Files:
*   **`pcbnew/exporters/export_hyperlynx.cpp`**
    *   **Function:** `HYPERLYNX_EXPORTER::writeDevices`
    *   **Change:** Added logic to read a custom footprint field named `GDS_FILE` and emit it on the device line when non-empty.
    *   **Logic:**
        ```cpp
        PCB_FIELD* gdsField = footprint->GetField( wxT( "GDS_FILE" ) );
        if( gdsField )
        {
            wxString gdsPath = gdsField->GetText();
            if( !gdsPath.IsEmpty() )
            {
                // emit ... GDS_FILE="<gdsPath>" on the device line
            }
        }
        ```
    *   **Implication:**
        *   If a user adds a field `GDS_FILE` = `/path/to/cell.gds` to a footprint, the device line in the exported `.hyp` file carries the path, e.g.:
            `(? REF="U1" L="Top" X=... Y=... R=... GDS_FILE="/path/to/cell.gds")`
        *   This enables integration with tools that map PCB components to GDS cells.

## 8. HyperLynx Export: Explicit Via Layer Stacks (2025-12-04)

**Objective:** Ensure through-hole vias are exported with their full layer stack definitions instead of the optimized `MDEF` (Master Definition) tag. This is required for downstream tools (like `hyp_to_gds.py`) to correctly map via geometry to specific PDK layers.

### Modified Files:
*   **`pcbnew/exporters/export_hyperlynx.cpp`**
    *   **Function:** `HYPERLYNX_EXPORTER::writeSinglePadStack`
    *   **Change:** Removed the `if( outLayers == layerMask )` optimization branch that collapsed full-stack vias into `MDEF`; the function now always lists explicit layers (per-layer loop). The inline comment notes this helps downstream tools map to specific PDK layers.
    *   **Implication:** All vias now export as an explicit list of layers, e.g.:
        ```
        {PADSTACK=14, 0.000003000
          ("TopMetal2", 0, 0.000005000, 0.000005000, 0.0, M)
          ("TopMetal1", 0, 0.000005000, 0.000005000, 0.0, M)
          ...
        }
        ```

## 9. Chiplet Assembly Export (2025-11-28)

**Objective:** Export the board placement data in a `.chiplet` YAML format suitable for 3D assembly tools, linking KiCad footprints to GDSII files and technology (`.lyp`) definitions.

### Modified Files:
*   **`pcbnew/exporters/export_chiplet.cpp`** (New file)
    *   **Class:** `CHIPLET_EXPORTER : public BOARD_EXPORTER_BASE`
    *   **Entry point:** `Run()` only opens the output file (`LOCALE_IO` for C-locale floats) and delegates to `WriteYAML( f )` inside a try/catch. All export logic lives in `WriteYAML`:
        1.  Reads `INTERPOSER_LYP` (from board properties, falling back to project text variables) to define the interposer technology. Optional `INTERPOSER_ADAPTER` and `INTERCONNECT_ADAPTER` text variables select the ADK PDK adapters.
        2.  Iterates over footprints, segregating I/O pads (footprints carrying an `IO_CLASS` field) from chiplet dies, and reads each die's `GDS_FILE` and `LYP_FILE` fields.
        3.  Computes component dimensions (`width`, `height`) from the footprint courtyard (`F_CrtYd` / `B_CrtYd`) when present, falling back to the footprint bounding box. Conversion to micrometers uses the static `iu2um` helper.
        4.  Transforms coordinates from KiCad global (Y-down) to Cartesian (Y-up), relative to the board `Edge.Cuts` bounding box:
            *   `int interposer_x_min = boardBBox.GetX();`
            *   `int interposer_y_max = boardBBox.GetBottom();`
            *   `x_out = iu2um( pos.x - interposer_x_min )`
            *   `y_out = iu2um( interposer_y_max - pos.y )`
        5.  Emits YAML with `assembly.units: "um"`. The file is written in the intermediate `pcb-bbox-corner` frame: it carries a `_metadata` block with `frame: pcb-bbox-corner` and `finalize_required: true`. The canonical `.chiplet` frame is GDS-bbox-corner; the downstream finalizer (`hyp_to_gds.py --update-chiplet-file`) converts to the canonical frame and strips the `_metadata` block. Readers MUST refuse files with `finalize_required: true`.

*   **`pcbnew/exporters/export_chiplet.h`**
    *   Declares the SWIG-callable headless entry point `bool ExportBoardToChipletFile( BOARD* aBoard, const wxString& aOutputPath );` (defined at the end of `export_chiplet.cpp`).

### Invocation (refactored: plugin-driven, not a menu action)

The legacy C++ pcbnew menu actions ("File > Export > Chiplet..." and "File > Export > Hyperlynx...") were removed from this fork. There is no `exportChiplet` action, no `ExportChiplet` handler, and no Export-submenu entry in `pcbnew/menubar_pcb_editor.cpp`, `pcbnew/tools/pcb_actions.{cpp,h}`, or `pcbnew/tools/board_editor_control.cpp`.

Export is now driven by the **Chiplets KiCad plugin** (Tools > External Plugins > Chiplet Export). The underlying C++ exporters remain as SWIG-callable headless functions that the plugin (or any Python script) invokes:
*   `ExportBoardToChipletFile( BOARD*, const wxString& )` in `export_chiplet.cpp` / `export_chiplet.h`.
*   `ExportBoardToHyperlynxFile( BOARD*, const wxString& )` in `export_hyperlynx.cpp` / `export_hyperlynx.h`.

Both headers are exposed to Python via `pcbnew/python/swig/pcbnew.i` (`#include` and `%include` of `exporters/export_chiplet.h` and `exporters/export_hyperlynx.h`). `BOARD_STACKUP` / `BOARD_STACKUP_ITEM` are additionally exposed via `pcbnew/python/swig/board_stackup.i` (`%include`d from `board.i`), an ADK addition so the Python Hyperlynx writer can iterate the physical stackup.

### Usage:
1.  **Setup:** Define `INTERPOSER_LYP` in **Board Setup > Text Variables** with the path to the interposer's `.lyp` file. Optionally set `INTERPOSER_ADAPTER` / `INTERCONNECT_ADAPTER`.
2.  **Components:** Ensure footprints have `GDS_FILE` and optional `LYP_FILE` fields. Footprints with an `IO_CLASS` field are treated as interposer I/O pads.
3.  **Export:** Run the Chiplets plugin from **Tools > External Plugins > Chiplet Export**, which calls `ExportBoardToChipletFile`. Run the downstream finalizer `hyp_to_gds.py --update-chiplet-file` to convert the intermediate `pcb-bbox-corner` frame to the canonical GDS-bbox-corner frame.

## 10. Nanometer-scale Numeric Correctness

**Objective:** Stop 32-bit distance/coordinate accumulation from overflowing at nanometer internal-unit scale, where a delta above a few tens of micrometers (about 46 um per axis) exceeds `INT_MAX` once squared.

### Modified Files:
*   **`pcbnew/board.cpp`**
    *   **Function:** `BOARD::FindFootprintByPosition` (selection hit-test).
    *   **Change:** `min_dim` / `alt_min_dim` widened from `int` (`0x7FFFFFFF`) to `int64_t` (`INT64_MAX`), and the squared-distance accumulation cast to `int64_t`. A 32-bit square overflowed at nm IU scale, so the nearest-footprint search could pick the wrong item.
*   **`pcbnew/router/pns_line.cpp`**
    *   **Change:** `lastDst` and the squared-distance `d` widened from `int` (`INT_MAX`) to `int64_t` (`INT64_MAX`) for the same reason.
*   **`pcbnew/router/pns_topology.cpp`**
    *   **Change:** The differential-pair gap computation switched from a truncating `(int)` cast to `KiROUND( ... )`, so the gap is rounded rather than floored at nanometer scale.

## 11. Zone Filler (nanometer epsilon)

**Objective:** Replace a fixed 1 um pruning epsilon that skipped zone pruning for features below roughly 4 um.

### Modified Files:
*   **`pcbnew/zone_filler.cpp`**
    *   **Change:** The fill epsilon moved from `pcbIUScale.mmToIU( 0.001 )` (1 um) to `std::max( half_min_width / 10, 1 )` (a feature-proportional value, minimum 1 IU); the hatch margin likewise became `std::max( aZone->GetMinThickness() / 10, 1 )`.
    *   **Implication:** Zone fills behave correctly at nanometer scale; note this also changes standard-PCB-scale fill behavior (10% of the min width instead of the old fixed 1 um).

## 12. Stability Fixes

**Objective:** A non-nanometer correctness fix carried in this fork.

### Modified Files:
*   **`common/eda_base_frame.cpp`, `eeschema/sch_edit_frame.cpp`, `pcbnew/pcb_edit_frame.cpp`**
    *   **Change:** Autosave history savers are keyed by stable addresses instead of mutable pointers that `SetBoard()` / `SetSchematic()` delete and replace, fixing a use-after-free.
