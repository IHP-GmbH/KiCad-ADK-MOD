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
#include <wx/filedlg.h>
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
#include <tools/board_editor_control.h>
#include <exporters/board_exporter_base.h>
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
        std::vector<FOOTPRINT*> components;

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

        wxString interposerTechID = "interposer_tech";
        if( !interposerLyp.IsEmpty() )
        {
            techMap[interposerTechID] = interposerLyp;
        }
        else
        {
            wxLogWarning( "Interposer LYP path not found. Set 'INTERPOSER_LYP' in Board Setup > Text Variables." );
        }

        // 2. Identify Footprint Technologies
        for( FOOTPRINT* footprint : m_board->Footprints() )
        {
            if( footprint->GetAttributes() & FP_EXCLUDE_FROM_BOM )
                continue; // Skip virtual components

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

        // Assembly Section
        fprintf( f, "assembly:\n" );
        wxString assemblyName = m_board->GetFileName();
        wxFileName fn( assemblyName );
        fprintf( f, "  name: \"%s\"\n", (const char*) fn.GetName().utf8_str() );
        fprintf( f, "  units: \"um\"\n" );
        fprintf( f, "\n" );

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

        // Components Section
        fprintf( f, "components:\n" );

        // 1. The Interposer itself
        BOX2I boardBBox = m_board->GetBoardEdgesBoundingBox();
        double width = iu2um( boardBBox.GetWidth() );
        double height = iu2um( boardBBox.GetHeight() );
        double thickness = iu2um( m_board->GetDesignSettings().GetBoardThickness() );
        
        fprintf( f, "  - id: interposer\n" );
        fprintf( f, "    type: interposer\n" );
        if( techMap.count( interposerTechID ) )
            fprintf( f, "    technology: %s\n", (const char*) interposerTechID.utf8_str() );
        
        wxString gdsName = fn.GetName() + ".gds";
        fprintf( f, "    layout: \"%s\"\n", (const char*) gdsName.utf8_str() );
        fprintf( f, "    top_cell: \"TOP\"\n" ); 
        
        fprintf( f, "    dimensions:\n" );
        fprintf( f, "      width: %.4f\n", width );
        fprintf( f, "      height: %.4f\n", height );
        fprintf( f, "      thickness: %.4f\n", thickness );
        
        fprintf( f, "    position:\n" );
        fprintf( f, "      x: %.4f\n", iu2um( boardBBox.GetX() ) );
        fprintf( f, "      y: %.4f\n", iu2um( boardBBox.GetY() ) );
        fprintf( f, "      z: 0.0\n" );
        fprintf( f, "\n" );

        // 2. Components
        for( FOOTPRINT* footprint : components )
        {
            wxString ref = footprint->GetReference();
            wxString gds = footprint->GetField( wxT( "GDS_FILE" ) ) ? footprint->GetField( wxT( "GDS_FILE" ) )->GetText() : wxString("");
            
            fprintf( f, "  - id: %s\n", (const char*) ref.utf8_str() );
            fprintf( f, "    type: die\n" ); 
            
            if( m_componentTechs.count( footprint ) )
                fprintf( f, "    technology: %s\n", (const char*) m_componentTechs[footprint].utf8_str() );

            if( !gds.IsEmpty() )
                fprintf( f, "    layout: \"%s\"\n", (const char*) gds.utf8_str() );

            // Dimensions
            // Use Courtyard if available, otherwise BoundingBox
            BOX2I fpBBox;
            // GetCurrentCourtyard returns a SHAPE_POLY_SET, we can get its bbox
            // But FOOTPRINT has GetBoundingBox() which often includes text.
            // Try to get body-only bbox if possible.
            // footprint->GetBoundingBox( false, false ) ? (depends on implementation)
            // Let's use GetBoundingBox( false ) -> excludes invisible text usually?
            // Actually, footprint->GetBoundingBox() includes everything visible.
            
            // Ideally we want the courtyard bbox.
            fpBBox = footprint->GetBoundingBox(); // Fallback
            
            // Try to find courtyard for the front layer (usually where component is)
            // But components can be on Back.
            PCB_LAYER_ID layer = footprint->GetLayer();
            const SHAPE_POLY_SET& courtyard = footprint->GetCurrentCourtyard();
            if( !courtyard.IsEmpty() )
            {
                fpBBox = courtyard.BBox();
            }

            double compWidth = iu2um( fpBBox.GetWidth() );
            double compHeight = iu2um( fpBBox.GetHeight() );
            
            // Thickness? KiCad footprints don't usually have thickness unless in 3D settings.
            // Let's default to 0 or check 3D settings if complex.
            // For now 0 is safe or skip.
            
            fprintf( f, "    dimensions:\n" );
            fprintf( f, "      width: %.4f\n", compWidth );
            fprintf( f, "      height: %.4f\n", compHeight );
            fprintf( f, "      thickness: 0.0\n" ); // Placeholder

            // Position
            VECTOR2I pos = footprint->GetPosition();
            
            fprintf( f, "    position:\n" );
            fprintf( f, "      x: %.4f\n", iu2um( pos.x ) );
            fprintf( f, "      y: %.4f\n", iu2um( pos.y ) );
            
            if( footprint->GetLayer() == F_Cu )
                fprintf( f, "      z: %.4f\n", thickness ); 
            else
                fprintf( f, "      z: 0.0\n" );
            
            fprintf( f, "    rotation:\n" );
            fprintf( f, "      z: %.4f\n", footprint->GetOrientation().AsDegrees() );

            fprintf( f, "\n" );
        }
    }

    std::map<FOOTPRINT*, wxString> m_componentTechs;
};


int BOARD_EDITOR_CONTROL::ExportChiplet( const TOOL_EVENT& aEvent )
{
    wxString    wildcard =  wxT( "*.chiplet" );
    BOARD*      board = m_frame->GetBoard();
    wxFileName  fn = board->GetFileName();

    fn.SetExt( wxT("chiplet") );

    wxFileDialog dlg( m_frame, _( "Export Chiplet Assembly" ), fn.GetPath(), fn.GetFullName(),
                      wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() != wxID_OK )
        return 0;

    fn = dlg.GetPath();
    fn.SetExt( wxT( "chiplet" ) );

    CHIPLET_EXPORTER exporter( board );
    exporter.SetOutputFilename( fn );
    exporter.Run();

    return 0;
}