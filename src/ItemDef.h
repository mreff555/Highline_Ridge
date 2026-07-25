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

#ifndef ITEM_DEF_H
#define ITEM_DEF_H

#include <AudioTypes.h>
#include <string>
#include <vector>

namespace timberline_engine
{

struct ItemVisualDef
{
    std::string image;
    std::string alternateImage;
    std::string alternateImageFlag;
};

struct ItemIconDef
{
    std::string icon;
    std::string alternateIcon;
    std::string alternateIconFlag;
};

struct ItemTtsDef
{
    bool enabled = false;
    std::string voice;
    std::string text;
    std::string audio;
};

struct ItemSfxDef
{
    std::string open;
    std::string close;
    std::string examine;
    std::string use;
};

struct ItemAudioOverlayDef
{
    AudioClipDef music;
    bool hasMusic = false;
    std::vector<AudioClipDef> ambient;
    float sceneMix = 1.0f;
    bool muteSceneAudio = false;
};

struct ItemContainerContentDef
{
    std::string itemId;
    int quantity = 1;
    bool hidden = false;
    std::string revealFlag;
    bool extractable = true;
    std::string examineNote;
};

struct ItemContainerDef
{
    bool isContainer = false;
    bool decomposes = false;
    std::vector<ItemContainerContentDef> contents;
};

struct ItemQuantityDef
{
    bool stackable = false;
    int defaultQuantity = 1;
    float unitWeightLb = 0.0f;
    std::string unitLabel;
};

struct ItemDef
{
    std::string id;
    std::string name;
    std::string description;
    std::string alternateDescription;
    std::string alternateDescriptionFlag;
    float weightLb = 0.0f;
    ItemVisualDef visuals;
    ItemIconDef icons;
    ItemTtsDef examineTts;
    ItemSfxDef sfx;
    ItemAudioOverlayDef examineAudio;
    ItemContainerDef container;
    ItemQuantityDef quantity;
    bool lightSource = false;
    // Two-stage examine/use items: examineRevealFlag on back from examine;
    // useRequiresFlag gates Use from the list (examining counts as satisfied);
    // useRevealFlag + visuals.alternateImage swap the examine image in place;
    // back still returns to the item list.
    std::string examineRevealFlag;
    std::string useRequiresFlag;
    std::string useRevealFlag;
    std::string useNarrative;
};

float roundItemWeightLb(float weightLb);
bool hasItemFlag(const std::vector<std::string>& activeFlags, const std::string& flag);
std::string resolveItemPath(
    const std::string& primaryPath,
    const std::string& alternatePath,
    const std::string& alternateFlag,
    const std::vector<std::string>& activeFlags);

}

#endif /* ITEM_DEF_H */