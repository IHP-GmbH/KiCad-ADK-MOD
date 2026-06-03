/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2025 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <cstdio>
#include <vector>
#include <map>
#include <set>

#include <wx/log.h>
#include <wx/msgdlg.h>

#include <kiface_base.h>
#include <macros.h>
#include <pcb_edit_frame.h>
#include <board.h>
#include <project.h>
#include <board_design_settings.h>
#include <board_item.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_track.h>
#include <zone.h>
#include <ki_exception.h>
#include <locale_io.h>
#include <reporter.h>
#include <richio.h>
#include <string_utils.h>
#include <exporters/board_exporter_base.h>
#include <exporters/export_chiplet.h>
#include <math/box2.h>

// Helper to convert internal units to micrometers
static double iu2um( double iu )
{
    // PCB_IU_PER_MM is usually 10000 or 1000000 depending on config.
    // We want micrometers.
    return iu / ( PCB_IU_PER_MM / 1000.0 );
}

class CHIPLET_EXPORTER : public BOARD_EXPORTER_BASE
{
public:
    CHIPLET_EXPORTER( BOARD* aBoard )
    {
        SetBoard( aBoard );
    }

    bool Run() override
    {
        if( !m_board )
            return false;

        FILE* f = wxFopen( m_outputFilePath.GetFullPath(), wxT( "wt" ) );

        if( !f )
        {
            wxMessageBox( _( "Could not open file for writing." ), _( "Export Error" ) );
            return false;
        }

        LOCALE_IO   toggle; // Switch to C locale for float printing

        try
        {
            WriteYAML( f );
        }
        catch( const IO_ERROR& ioe )
        {
            wxMessageBox(ioe.What(), _( "Export Error" ) );
            fclose( f );
            return false;
        }

        fclose( f );
        return true;
    }

private:
    void WriteYAML( FILE* f )
    {
        // Data Gathering
        std::map<wxString, wxString> techMap; // TechID -> LypPath
        std::vector<FOOTPRINT*> components;     // chiplet dies
        std::vector<FOOTPRINT*> ioPadComponents; // wire-bond / I/O pads, nested under interposer

        // 1. Identify Interposer Technology
        wxString interposerLyp = "";
        if ( m_board->GetProperties().count( "INTERPOSER_LYP" ) )
            interposerLyp = m_board->GetProperties().at( "INTERPOSER_LYP" );

        if( interposerLyp.IsEmpty() )
        {
            // Fallback: Check if defined as a text variable in project settings
            if( m_board->GetProject() )
            {
                const std::map<wxString, wxString>& vars = m_board->GetProject()->GetTextVars();
                if( vars.count( "INTERPOSER_LYP" ) )
                    interposerLyp = vars.at( "INTERPOSER_LYP" );
            }
        }

        wxString interposerTechID = "intm4tm2";
        if( !interposerLyp.IsEmpty() )
        {
            techMap[interposerTechID] = interposerLyp;
        }
        else
        {
            wxLogWarning( "Interposer LYP path not found. Set 'INTERPOSER_LYP' in Board Setup > Text Variables." );
        }

        // 2. Identify Footprint Technologies and segregate I/O pads from chiplet dies.
        // Footprints carrying an IO_CLASS field (e.g. "wire_bond") are part of the
        // interposer itself -- they belong nested under interposer.io_pads, not as
        // separate top-level die components.
        for( FOOTPRINT* footprint : m_board->Footprints() )
        {
            if( footprint->GetAttributes() & FP_EXCLUDE_FROM_BOM )
                continue; // Skip virtual components

            wxString ioClass = footprint->GetField( wxT( "IO_CLASS" ) ) ? footprint->GetField( wxT( "IO_CLASS" ) )->GetText() : wxString("");
            if( !ioClass.IsEmpty() )
            {
                ioPadComponents.push_back( footprint );
                continue;
            }

            // Get Fields
            wxString gdsFile = footprint->GetField( wxT( "GDS_FILE" ) ) ? footprint->GetField( wxT( "GDS_FILE" ) )->GetText() : wxString("");
            wxString lypFile = footprint->GetField( wxT( "LYP_FILE" ) ) ? footprint->GetField( wxT( "LYP_FILE" ) )->GetText() : wxString("");

            if( gdsFile.IsEmpty() )
            {
                 wxLogWarning( "Footprint %s missing GDS_FILE field.", footprint->GetReference() );
            }

            if( !lypFile.IsEmpty() )
            {
                // Simple ID generation based on filename
                wxFileName fn( lypFile );
                wxString techID = fn.GetName();
                techID.Replace( " ", "_" );
                techID.Replace( ".", "_" );

                techMap[techID] = lypFile;
                m_componentTechs[footprint] = techID;
            }

            components.push_back( footprint );
        }

        // --- Write File ---

        // Header
        fprintf( f, "format_version: \"1.0\"\n\n" );

        // Intermediate-frame marker. KiCad's exporter computes positions
        // against the PCB Edge.Cuts bounding box, which is generally
        // shifted relative to the GDS routing bbox. The canonical
        // .chiplet frame is GDS-bbox-corner; see
        // chiplet-studio/docs/coord_frame_contract.md (sections 1 and
        // 4.1). The downstream finalizer (hyp_to_gds.py
        // --update-chiplet-file) converts to the canonical frame and
        // strips this block. Readers MUST refuse files with
        // finalize_required: true.
        fprintf( f, "_metadata:\n" );
        fprintf( f, "  frame: pcb-bbox-corner\n" );
        fprintf( f, "  finalize_required: true\n" );
        fprintf( f, "  finalizer: \"hyp_to_gds.py --update-chiplet-file\"\n" );
        fprintf( f, "\n" );

        // Assembly Section
        fprintf( f, "assembly:\n" );
        wxString assemblyName = m_board->GetFileName();
        wxFileName fn( assemblyName );
        fprintf( f, "  name: \"%s\"\n", (const char*) fn.GetName().utf8_str() );
        fprintf( f, "  units: \"um\"\n" );
        fprintf( f, "\n" );

        // Interposer adapter. Declares which ADK PDK adapter the assembly DRC
        // resolves. Mirrors the Python writer (chiplet_writer.py); override via
        // Board Setup > Text Variables > INTERPOSER_ADAPTER.
        wxString interposerAdapter = "intm4tm2";
        if( m_board->GetProject() )
        {
            const std::map<wxString, wxString>& vars = m_board->GetProject()->GetTextVars();
            if( vars.count( "INTERPOSER_ADAPTER" ) && !vars.at( "INTERPOSER_ADAPTER" ).IsEmpty() )
                interposerAdapter = vars.at( "INTERPOSER_ADAPTER" );
        }
        fprintf( f, "interposer:\n" );
        fprintf( f, "  adapter: \"%s\"\n", (const char*) interposerAdapter.utf8_str() );
        fprintf( f, "\n" );

        // Interconnect adapter (optional second axis). Emitted only when the
        // INTERCONNECT_ADAPTER text variable is set, mirroring the Python writer
        // (an unset value yields no block -> no interconnect axis).
        wxString interconnectAdapter = "";
        if( m_board->GetProject() )
        {
            const std::map<wxString, wxString>& vars = m_board->GetProject()->GetTextVars();
            if( vars.count( "INTERCONNECT_ADAPTER" ) )
                interconnectAdapter = vars.at( "INTERCONNECT_ADAPTER" );
        }
        if( !interconnectAdapter.IsEmpty() )
        {
            fprintf( f, "interconnect:\n" );
            fprintf( f, "  adapter: \"%s\"\n", (const char*) interconnectAdapter.utf8_str() );
            fprintf( f, "\n" );
        }

        // Technologies Section
        fprintf( f, "technologies:\n" );
        for( auto const& [id, path] : techMap )
        {
            fprintf( f, "  %s:\n", (const char*) id.utf8_str() );
            fprintf( f, "    description: \"Imported from KiCad\"\n" );
            fprintf( f, "    layer_properties: \"%s\"\n", (const char*) path.utf8_str() );
            fprintf( f, "    dbu: 0.001\n" ); // Defaulting to 1nm
            fprintf( f, "\n" );
        }

        // Connection Stacks Section (default bump technology library)
        fprintf( f, "connection_stacks:\n" );
        fprintf( f, "  cupillar_opt1:\n" );
        fprintf( f, "    description: \"PacTech Cu Pillar, Table 6.1 Option 1 (35um opening)\"\n" );
        fprintf( f, "    layers:\n" );
        fprintf( f, "      - {name: CuPillar, material: Cu, height: 28.0, diameter: 44.0}\n" );
        fprintf( f, "      - {name: SnAgCap, material: SnAg, height: 16.0, diameter: 44.0}\n" );
        fprintf( f, "  cupillar_opt2:\n" );
        fprintf( f, "    description: \"PacTech Cu Pillar, Table 6.1 Option 2 (40um opening)\"\n" );
        fprintf( f, "    layers:\n" );
        fprintf( f, "      - {name: CuPillar, material: Cu, height: 32.0, diameter: 49.0}\n" );
        fprintf( f, "      - {name: SnAgCap, material: SnAg, height: 16.0, diameter: 49.0}\n" );
        fprintf( f, "  cupillar_opt3:\n" );
        fprintf( f, "    description: \"PacTech Cu Pillar, Table 6.1 Option 3 (45um opening)\"\n" );
        fprintf( f, "    layers:\n" );
        fprintf( f, "      - {name: CuPillar, material: Cu, height: 42.0, diameter: 54.0}\n" );
        fprintf( f, "      - {name: SnAgCap, material: SnAg, height: 19.0, diameter: 54.0}\n" );
        fprintf( f, "  sbump_sac305:\n" );
        fprintf( f, "    description: \"PacTech SAC305 solder bump (80um ball)\"\n" );
        fprintf( f, "    layers:\n" );
        fprintf( f, "      - {name: SolderBall, material: SAC305, height: 80.0, diameter: 80.0}\n" );
        fprintf( f, "\n" );

        // Components Section
        fprintf( f, "components:\n" );

        // 1. The Interposer itself
        BOX2I boardBBox = m_board->GetBoardEdgesBoundingBox();

        // Fallback for designs without Edge.Cuts shapes (e.g. nm-scale GDS-derived
        // interposers): derive the bbox from the union of footprints/tracks/zones.
        if( !boardBBox.IsValid() || boardBBox.GetWidth() == 0 || boardBBox.GetHeight() == 0 )
            boardBBox = m_board->ComputeBoundingBox( false );

        double width = iu2um( boardBBox.GetWidth() );
        double height = iu2um( boardBBox.GetHeight() );
        double thickness = iu2um( m_board->GetDesignSettings().GetBoardThickness() );
        
        // KiCad Coordinates of the Interposer Bounding Box
        // X grows right, Y grows down.
        // We want to normalize so the bottom-left of the interposer (in Cartesian) is (0,0) or relative.
        // In KiCad, "Bottom-Left" visually is (x_min, y_max).
        
        int interposer_x_min = boardBBox.GetX();
        int interposer_y_max = boardBBox.GetBottom(); // Y is max at bottom in KiCad

        fprintf( f, "  - id: interposer\n" );
        fprintf( f, "    type: interposer\n" );
        // Per chiplet-studio/docs/coord_frame_contract.md section 2: the
        // interposer mesh is centered on its own GDS bbox; position
        // places that bbox center in the canonical frame.
        fprintf( f, "    anchor: bbox_center\n" );
        if( techMap.count( interposerTechID ) )
            fprintf( f, "    technology: %s\n", (const char*) interposerTechID.utf8_str() );
        
        wxString gdsName = fn.GetName() + ".gds";
        fprintf( f, "    layout: \"%s\"\n", (const char*) gdsName.utf8_str() );
        fprintf( f, "    top_cell: \"INTERPOSER\"\n" );
        
        fprintf( f, "    dimensions:\n" );
        fprintf( f, "      width: %.6f\n", width );
        fprintf( f, "      height: %.6f\n", height );
        fprintf( f, "      thickness: %.6f\n", thickness );
        
        // Position is a placeholder in this intermediate frame. KiCad does
        // not have access to the interposer GDS bounding box, so it cannot
        // compute the canonical interposer position (bbox center in the
        // GDS-bbox-corner frame). The downstream finalizer
        // (hyp_to_gds.py --update-chiplet-file) reads the GDS bbox and
        // writes the canonical value. The _metadata block above marks the
        // file as not-yet-canonical; chiplet-studio refuses to load it
        // directly. See chiplet-studio/docs/coord_frame_contract.md §4.1.
        fprintf( f, "    position:\n" );
        fprintf( f, "      x: 0.0\n" );
        fprintf( f, "      y: 0.0\n" );
        fprintf( f, "      z: 0.0\n" );

        // I/O pads nested under the interposer (wire-bond / future flipped_bump / tsv_bump).
        // Each pad keeps its KiCad reference as id, and its position/size are projected to
        // interposer-local micrometer coordinates (Y inverted to match the GDS Y-up convention).
        if( !ioPadComponents.empty() )
        {
            fprintf( f, "    io_pads:\n" );
            for( FOOTPRINT* fp : ioPadComponents )
            {
                wxString ref     = fp->GetReference();
                wxString ioClass = fp->GetField( wxT( "IO_CLASS" ) )->GetText();
                wxString sizeStr = fp->GetField( wxT( "IO_PAD_SIZE_UM" ) ) ? fp->GetField( wxT( "IO_PAD_SIZE_UM" ) )->GetText() : wxString("");

                double size_x_um = 0.0;
                double size_y_um = 0.0;
                if( !sizeStr.IsEmpty() )
                {
                    // Expected format: "WxH" e.g. "100x100"
                    wxArrayString parts = wxSplit( sizeStr, 'x' );
                    if( parts.size() == 2 )
                    {
                        parts[0].ToDouble( &size_x_um );
                        parts[1].ToDouble( &size_y_um );
                    }
                }
                if( size_x_um <= 0.0 || size_y_um <= 0.0 )
                {
                    // Fallback: derive from the first pad's geometry
                    if( !fp->Pads().empty() )
                    {
                        PAD* pad = fp->Pads().front();
                        size_x_um = iu2um( pad->GetSizeX() );
                        size_y_um = iu2um( pad->GetSizeY() );
                    }
                }

                wxString netName = "";
                if( !fp->Pads().empty() )
                    netName = fp->Pads().front()->GetNetname();

                VECTOR2I pos = fp->GetPosition();
                double x_um = iu2um( pos.x - interposer_x_min );
                double y_um = iu2um( interposer_y_max - pos.y );

                fprintf( f, "      - id: %s\n", (const char*) ref.utf8_str() );
                fprintf( f, "        io_class: %s\n", (const char*) ioClass.utf8_str() );
                fprintf( f, "        net: \"%s\"\n", (const char*) netName.utf8_str() );
                fprintf( f, "        position: { x: %.6f, y: %.6f }\n", x_um, y_um );
                fprintf( f, "        size: { x: %.3f, y: %.3f }\n", size_x_um, size_y_um );
                fprintf( f, "        layer: TopMetal2\n" );
            }
        }
        fprintf( f, "\n" );

        // 2. Components
        for( FOOTPRINT* footprint : components )
        {
            wxString ref = footprint->GetReference();
            wxString gds = footprint->GetField( wxT( "GDS_FILE" ) ) ? footprint->GetField( wxT( "GDS_FILE" ) )->GetText() : wxString("");
            wxString orient = footprint->GetField( wxT( "ORIENTATION" ) ) ? footprint->GetField( wxT( "ORIENTATION" ) )->GetText() : wxString("");
            bool isFlipChip = ( orient == wxT( "flip_chip" ) );

            fprintf( f, "  - id: %s\n", (const char*) ref.utf8_str() );
            fprintf( f, "    type: die\n" );
            // Per chiplet-studio/docs/coord_frame_contract.md sections 2
            // and 4.4: dies produced by gds_to_kicad have GDS (0,0) as
            // the footprint anchor; the mesh is built around that
            // origin.
            fprintf( f, "    anchor: gds_origin\n" );

            if( m_componentTechs.count( footprint ) )
                fprintf( f, "    technology: %s\n", (const char*) m_componentTechs[footprint].utf8_str() );

            if( isFlipChip )
            {
                fprintf( f, "    connection: cupillar_opt2\n" );
            }

            if( !gds.IsEmpty() )
                fprintf( f, "    layout: \"%s\"\n", (const char*) gds.utf8_str() );

            if( isFlipChip )
            {
                fprintf( f, "    orientation: flip_chip\n" );
            }

            // Dimensions
            BOX2I fpBBox;
            fpBBox = footprint->GetBoundingBox(); // Fallback
            
            PCB_LAYER_ID layer = footprint->GetLayer();
            // GetCourtyard returns a reference to SHAPE_POLY_SET for the given layer
            // We need to determine which courtyard layer to check based on the footprint's side.
            // Usually F_CrtYd or B_CrtYd.
            PCB_LAYER_ID courtyardLayer = ( layer == B_Cu ) ? B_CrtYd : F_CrtYd;
            
            const SHAPE_POLY_SET& courtyard = footprint->GetCourtyard( courtyardLayer );
            if( !courtyard.IsEmpty() )
            {
                fpBBox = courtyard.BBox();
            }

            double compWidth = iu2um( fpBBox.GetWidth() );
            double compHeight = iu2um( fpBBox.GetHeight() );
            
            fprintf( f, "    dimensions:\n" );
            fprintf( f, "      width: %.6f\n", compWidth );
            fprintf( f, "      height: %.6f\n", compHeight );
            fprintf( f, "      thickness: 0.0\n" ); // Placeholder

            // Position Transformation
            // x_out = x_kicad - interposer_x_min
            // y_out = interposer_y_max - y_kicad
            
            VECTOR2I pos = footprint->GetPosition();
            
            double x_out = iu2um( pos.x - interposer_x_min );
            double y_out = iu2um( interposer_y_max - pos.y );
            
            fprintf( f, "    position:\n" );
            fprintf( f, "      x: %.6f\n", x_out );
            fprintf( f, "      y: %.6f\n", y_out );

            if( isFlipChip )
                fprintf( f, "      z: 0.0\n" ); // Auto-calculated by Chiplet Studio from connection stack
            else if( footprint->GetLayer() == F_Cu )
                fprintf( f, "      z: %.6f\n", thickness );
            else
                fprintf( f, "      z: 0.0\n" );
            
            fprintf( f, "    rotation:\n" );
            fprintf( f, "      z: %.4f\n", footprint->GetOrientation().AsDegrees() );

            fprintf( f, "\n" );
        }
    }

    std::map<FOOTPRINT*, wxString> m_componentTechs;
};


bool ExportBoardToChipletFile( BOARD* aBoard, const wxString& aOutputPath )
{
    if( !aBoard )
        return false;

    CHIPLET_EXPORTER exporter( aBoard );
    exporter.SetOutputFilename( wxFileName( aOutputPath ) );
    return exporter.Run();
}