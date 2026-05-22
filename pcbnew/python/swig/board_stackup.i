// SPDX-License-Identifier: GPL-2.0-or-later
// Expose BOARD_STACKUP / BOARD_STACKUP_ITEM and the stackup-item enums
// so Python plugins (e.g. the chiplet pipeline's Python Hyperlynx
// writer) can iterate the physical stackup that
// BOARD_DESIGN_SETTINGS::GetStackupDescriptor() returns.
//
// The vector template name is namespaced (BOARD_STACKUP_ITEM_Vector)
// to avoid collisions with other SWIG vector typedefs.

%ignore DIELECTRIC_PRMS;
%ignore BOARD_STACKUP::Serialize;
%ignore BOARD_STACKUP::Deserialize;

%{
#include <board_stackup_manager/board_stackup.h>
%}

%include <board_stackup_manager/board_stackup.h>

%template(BOARD_STACKUP_ITEM_Vector) std::vector<BOARD_STACKUP_ITEM*>;
