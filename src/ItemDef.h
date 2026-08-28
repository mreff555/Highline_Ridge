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
#include <TtsVoiceMarkup.h>
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

/** One ingredient line on a product item's craft recipe. */
struct ItemComponentDef
{
    std::string itemId;
    /** Other item ids that may fill this slot (e.g. knife or nails as tool). */
    std::vector<std::string> alternates;
    /** Minimum available quantity required (0 means "at least 1 present"). */
    int reqQty = 1;
    /**
     * When true, spend reqQty from the component stack (0 spend if reqQty is 0).
     * When false, presence-only / tool: no decrement (cleanup may still snap tools to 1).
     * If unset at parse time, defaults from the component item's consumeOnCombine.
     */
    bool consume = true;
    bool consumeSpecified = false;

    bool acceptsItemId(const std::string& id) const
    {
        if (itemId == id)
            return true;
        for (const std::string& alternate : alternates)
        {
            if (alternate == id)
                return true;
        }
        return false;
    }
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
    /** Item-local TTS gate. Off by default; independent of the current scene. */
    TtsOwnerPolicy ttsPolicy;
    ItemTtsDef examineTts;
    ItemTtsDef useTts;
    ItemTtsDef takeTts;
    ItemSfxDef sfx;
    ItemAudioOverlayDef examineAudio;
    ItemContainerDef container;
    ItemQuantityDef quantity;
    /**
     * Default when this item is used as a craft component and the product line
     * omits "consume". Tools should set false.
     */
    bool consumeOnCombine = true;
    /** Separate from combine; reserved for world/use actions. */
    bool consumeOnUse = true;
    /**
     * Product craft recipe. Non-empty => this item can be crafted by combining
     * the listed components (typically two). Order does not matter at runtime.
     */
    std::vector<ItemComponentDef> components;
    std::string assembleNarrative;
    ItemTtsDef assembleTts;
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

/** Default max inventory carry weight (lb). Later may scale with status. */
constexpr float kMaxCarryWeightLb = 50.0f;

float roundItemWeightLb(float weightLb);
bool hasItemFlag(const std::vector<std::string>& activeFlags, const std::string& flag);
std::string resolveItemPath(
    const std::string& primaryPath,
    const std::string& alternatePath,
    const std::string& alternateFlag,
    const std::vector<std::string>& activeFlags);

}

#endif /* ITEM_DEF_H */