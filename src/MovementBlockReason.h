/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#ifndef MOVEMENT_BLOCK_REASON_H
#define MOVEMENT_BLOCK_REASON_H

namespace timberline_engine
{

/** Why a advertised movement direction is currently unavailable. */
enum class MovementBlockReason
{
    None,       // available, or no exit in that direction
    NeedsLight, // dark destination / missing lantern or light source
    NeedsGear,  // missing required inventory gear (e.g. pick / crampons)
    NeedsLock,  // locked door / unmet story gate / room not purchased
    Other       // mixed or unclassified blockers
};

/** Per-direction overlay hints for MOVE buttons (indices match ButtonMgr 0–5). */
struct MovementBlockOverlays
{
    MovementBlockReason up = MovementBlockReason::None;
    MovementBlockReason down = MovementBlockReason::None;
    MovementBlockReason forward = MovementBlockReason::None;
    MovementBlockReason backward = MovementBlockReason::None;
    MovementBlockReason left = MovementBlockReason::None;
    MovementBlockReason right = MovementBlockReason::None;

    void clear()
    {
        *this = MovementBlockOverlays{};
    }
};

}

#endif /* MOVEMENT_BLOCK_REASON_H */
