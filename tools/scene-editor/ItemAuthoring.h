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

#ifndef TIMBERLINE_ITEM_AUTHORING_H
#define TIMBERLINE_ITEM_AUTHORING_H

#include "DocumentWorkspace.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace timberline_editor
{

/**
 * Author-facing capability switches. Mapped onto ItemDef / items.json fields
 * by applyPayloadToItemJson (not a 1:1 JSON mirror).
 *
 * Item-level TTS is controlled by descriptionTtsEnabled on the payload
 * (switch under Description), not a capability checkbox.
 */
struct ItemCapabilitySwitches
{
    bool stackable = false;
    bool tool = false;
    bool lightSource = false;
    bool consumeOnUse = true;
};

/**
 * Product craft recipe (two-component product). Driven by the Product Recipe
 * switch in the authoring dialog.
 */
struct ItemProductRecipe
{
    bool enabled = false;
    /** When true, show/edit assembleTts (TTS Construction Description). */
    bool ttsEnabled = false;
    std::string component1;
    std::string component2;
    std::string constructionDescription;
    std::string ttsConstructionDescription;
    /**
     * Optional full components JSON (Advanced). When non-empty and valid,
     * overrides component1/component2 on write.
     */
    std::string advancedComponentsJson;
};

struct ItemAiAssistSwitches
{
    bool generateImageFromDescription = false;
    bool generateIconFromDescription = false;
    bool generateExamineSound = false;
    bool generateUseSound = false;
    bool assistConstructionDescription = false;
    bool assistTtsConstructionDescription = false;
    bool assistTtsDescription = false;
};

/** Shared manual/AI payload for creating or replacing an item. */
struct ItemAuthoringPayload
{
    std::string id;
    std::string name;
    std::string description;
    float weightLb = 0.0f;
    ItemCapabilitySwitches capabilities;
    ItemProductRecipe recipe;
    ItemAiAssistSwitches aiAssist;

    std::string imagePath;
    std::string iconPath;

    /** Switch under Description — off by default. */
    bool descriptionTtsEnabled = false;
    /** examineTts spoken text (shown only when descriptionTtsEnabled). */
    std::string ttsDescription;

    /** SFX paths (not TTS). */
    std::string examineSoundPath;
    std::string useSoundPath;
};

enum class ItemAiAssistJobType
{
    GenerateImage,
    GenerateIcon,
    GenerateExamineSound,
    GenerateUseSound,
    GenerateConstructionDescription,
    GenerateTtsConstructionDescription,
    GenerateTtsDescription
};

struct ItemAiAssistJob
{
    ItemAiAssistJobType type = ItemAiAssistJobType::GenerateImage;
    std::string prompt;
    std::string outPath;
    std::string action;
};

struct ItemAiAssistPlan
{
    std::vector<ItemAiAssistJob> jobs;
    bool empty() const { return jobs.empty(); }
};

struct ItemAuthoringResult
{
    bool ok = false;
    std::string itemId;
    std::string error;
    ItemAiAssistPlan aiPlan;
    std::string aiStatus;
};

std::string slugifyItemId(const std::string& name);
std::string defaultItemExamineImagePath(const std::string& itemId);
std::string defaultItemIconPath(const std::string& itemId);
std::string defaultItemExamineSoundPath(const std::string& itemId);
std::string defaultItemUseSoundPath(const std::string& itemId);

bool loadPayloadFromItemJson(
    const std::string& itemId,
    const nlohmann::json& item,
    ItemAuthoringPayload& out,
    std::string& errorOut);

bool validateItemAuthoringPayload(
    const DocumentWorkspace& docs,
    ItemAuthoringPayload& payload,
    bool createOnly,
    std::string& errorOut);

nlohmann::json applyPayloadToItemJson(
    const ItemAuthoringPayload& payload,
    const nlohmann::json* existingOrNull);

ItemAiAssistPlan planItemAiAssist(const ItemAuthoringPayload& payload);

bool writeItemAiAssistJobsFile(
    const std::string& assetRoot,
    const std::string& itemId,
    const ItemAiAssistPlan& plan,
    std::string& errorOut);

ItemAuthoringResult upsertItemFromPayload(
    DocumentWorkspace& docs,
    ItemAuthoringPayload payload,
    bool createOnly);

/**
 * Run tools/run_item_authoring_ai.py for a jobs file written by upsert.
 * Generates images (xAI) and SFX (procedural MP3) for pending jobs.
 * apiKey is optional session key from the authoring UI (not written to disk).
 * Returns true when the runner exits 0.
 */
bool runItemAuthoringAiJobs(
    const std::string& assetRoot,
    const std::string& itemId,
    std::string& statusOut,
    const std::string& apiKey = {});

} // namespace timberline_editor

#endif /* TIMBERLINE_ITEM_AUTHORING_H */
