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

#ifndef LOCATION_STRUCT_H
#define LOCATION_STRUCT_H

#include <ActionStruct.h>
#include <ItemDef.h>
#include <MovementStruct.h>
#include <raylib.h>
#include <string>

namespace timberline_engine
{

struct LocationStruct
{
    Texture2D locationImage;
    bool ownsLocationImage = true;
    bool isUnderConstruction = false;
    std::string locationDescription;
    ItemTtsDef descriptionTts;
    std::string examineDetails;
    ItemTtsDef examineTts;
    std::string examineFlag;
    float examineLucidityDelta = 0.0f;
    bool examineLucidityOncePerDay = false;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    bool useRequiresExamine = true;
    bool useAdvancesDay = false;
    std::string useExit;
    Font descriptionFont;
    Font boldFont;
    Font uiFont;
    MovementStruct movementFilter;
    ActionStruct actionFilter;
};

}

#endif /* LOCATION_STRUCT_H */