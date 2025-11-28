# Technical Documentation: Nanometer & Micrometer Support

This document details the technical modifications applied to the KiCad codebase to support sub-micron features (down to 10nm), native Micrometer (µm) units in the UI, and high-precision export.

## 1. Internal Geometry Limits (10nm Support)

**Objective:** Lower the hardcoded safety limits that prevent creating features smaller than 0.01mm.

### Modified Files:
*   **`pcbnew/pcb_track.h`**
    *   **Symbol:** `GEOMETRY_MIN_SIZE`
    *   **Change:** Value changed from default (typically 0.01mm) to `0.00001 * pcbIUScale.IU_PER_MM` (10nm).
    *   **Implication:** Allows the router and DRC to accept track widths and via diameters down to 10nm.

*   **`include/board_design_settings.h`**
    *   **Symbols:** `MINIMUM_ERROR_SIZE_MM`, `MINIMUM_LINE_WIDTH_MM`
    *   **Change:** Defaults set to `0.00001`.
    *   **Implication:** Board setup constraints now allow entering values as small as 10nm without validation errors.

*   **`pcbnew/dialogs/dialog_track_via_size.cpp`**
    *   **Function:** `TransferDataFromWindow` (Validation logic)
    *   **Change:** Lowered `minSize` validation clamp to `0.00001 * pcbIUScale.IU_PER_MM`.
    *   **Implication:** The "Track & Via Properties" dialog accepts small values.

## 2. User Interface: Micrometer Units (µm)

**Objective:** Add "Micrometers" as a selectable display unit in the UI with sufficient precision.

### Modified Files:
*   **`common/eda_units.cpp`**
    *   **Function:** `EDA_UNIT_UTILS::UI::StringFromValue`
    *   **Change:** Increased display precision for `EDA_UNITS::UM`. Format string changed to `%.4f` (4 decimal places).
    *   **Implication:** 1nm is displayed as `0.0010 µm` instead of `0.0 µm` or `0 µm`.

*   **`common/tool/actions.cpp`, `include/tool/actions.h`**
    *   **Change:** Added `micrometersUnits` action definition.

*   **Toolbars (`pcbnew/toolbars_pcb_editor.cpp`, etc.)**
    *   **Change:** Added the µm button to the left toolbar alongside mm, inch, and mils.

## 3. Grid System

**Objective:** Provide usable grid presets for nanometer-scale design.

### Modified Files:
*   **`common/settings/app_settings.cpp`**
    *   **Function:** `APP_SETTINGS_BASE::DefaultGridSizeList`
    *   **Change:** Added the following presets to the "else" block (used by Pcbnew):
        *   `0.005 mm` (5 µm)
        *   `0.0025 mm` (2.5 µm)
        *   `0.001 mm` (1 µm)
        *   `0.0001 mm` (100 nm)
        *   `0.00001 mm` (10 nm)
        *   `0.000001 mm` (1 nm)
    *   **Implication:** These options appear in the grid dropdown menu. Note: KiCad internal units are integers (1 nm resolution), so 1nm is the hard floor for grid snapping.

## 4. Export Formats: Hyperlynx

**Objective:** Ensure export files retain nanometer precision. Standard Imperial/English exports lost precision due to floating point conversion factors (0.0254).

### Modified Files:
*   **`pcbnew/exporters/export_hyperlynx.cpp`**
    *   **Function:** `HYPERLYNX_EXPORTER::generateHeaders`
        *   **Change:** Header changed from `{UNITS=ENGLISH LENGTH}` to `{UNITS=METRIC LENGTH}`.
    *   **Function:** `iu2hyp` (Static helper)
        *   **Change:** Logic changed from:
            `return iu / 1e9 / 0.0254;` (Imperial)
            to:
            `return iu / ( PCB_IU_PER_MM * 1000.0 );` (Metric/Meters)
    *   **Implication:** 
        *   Exported files now use **Meters** as the base unit.
        *   Example: A 200µm trace appears as `W=0.0002000000`.
        *   Precision loss from Inch conversion is eliminated.

## 5. Rendering Fixes for Nanometer Scale (2025-11-27)

**Objective:** Fix visual artifacts where vias and tracks appeared with disproportionately large white borders/shadows at nanometer scales.

**Problem:** When working at nanometer scales (e.g., 10nm vias), some visual features—specifically the Locked Item Shadow and Hole Wall Painting—were using default millimeter-based sizes. For example, a standard shadow based on Silk Screen width (~0.15mm) would be 15,000 times larger than a 10nm via, appearing as a giant white halo.

### Modified Files:
*   **`pcbnew/pcb_painter.cpp`**
    *   **Function:** `PCB_PAINTER::draw( const PCB_VIA* aVia, int aLayer )`
        *   **Change (LAYER_LOCKED_ITEM_SHADOW):** Shadow width is now clamped to `via_width / 4` if `m_lockedShadowMargin` exceeds the via width.
        *   **Change (LAYER_VIA_HOLEWALLS):** Hole wall painting thickness is clamped to `drill_size / 4` if it exceeds the drill size.
    *   **Function:** `PCB_PAINTER::draw( const PCB_TRACK* aTrack, int aLayer )`
        *   **Change (LAYER_LOCKED_ITEM_SHADOW):** Shadow width is clamped to track width if `m_lockedShadowMargin` exceeds it.
    *   **Implication:** Visual artifacts are eliminated for nanometer-scale designs while maintaining normal appearance at standard PCB scales.

---

## 6. Build & Deployment (AppImage)

**Objective:** Create a portable, standalone executable that runs on the host machine without root privileges or Docker GUI issues.

### Build System Changes:
*   **`Dockerfile`:** 
    *   Updated base image to `ubuntu:24.04`.
    *   Added dependencies for AppImage creation: `wget`, `file`, `appstream`, `desktop-file-utils`, `patchelf`.
*   **`common/paths.cpp`:**
    *   **Change:** Modified `GetStockDataPath` to check for the `APPDIR` environment variable.
    *   **Reason:** Enables the AppImage to find its own resources (icons, shared files) relative to itself, rather than looking in system paths (`/usr/share`).
*   **`create_appimage.sh`:**
    *   Custom script created to bundle the compiled binaries, libraries, and resources into an AppImage using `linuxdeploy`.
    *   Manually copies `images.tar.gz` to ensure icons are present.
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
    *   **Change:** Added logic to check each footprint for a custom field named `GDS_FILE`.
    *   **Logic:**
        ```cpp
        PCB_FIELD* gdsField = footprint->GetField( wxT( "GDS_FILE" ) );
        if( gdsField && !gdsField->GetText().IsEmpty() ) {
            // Append GDS_FILE="path" to the device line
        }
        ```
    *   **Implication:**
        *   If a user adds a field `GDS_FILE` = `/path/to/cell.gds` to a footprint, the exported `.hyp` file will contain:
            `(? REF="U1" L="Top" GDS_FILE="/path/to/cell.gds")`
        *   This enables integration with tools that map PCB components to GDS cells.