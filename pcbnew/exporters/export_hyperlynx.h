/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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
 */

#ifndef EXPORT_HYPERLYNX_H
#define EXPORT_HYPERLYNX_H

#include <wx/string.h>

class BOARD;

/**
 * Export the board in Hyperlynx format (metric, meters).
 *
 * Used by the chiplet_kicad_plugin Python action plugin via SWIG.
 *
 * @param aBoard      board to export (must be non-null)
 * @param aOutputPath full path to the output .hyp file
 * @return true on success
 */
bool ExportBoardToHyperlynxFile( BOARD* aBoard, const wxString& aOutputPath );

#endif // EXPORT_HYPERLYNX_H
