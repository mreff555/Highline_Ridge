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

#include "ItemAuthoring.h"

#include "PlatformPath.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using timberline_engine::ensureDirectory;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

std::string trimCopy(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}

std::string firstSentence(const std::string& text)
{
    const std::string trimmed = trimCopy(text);
    if (trimmed.empty())
        return {};
    const size_t stop = trimmed.find_first_of(".!?");
    if (stop == std::string::npos)
        return trimmed;
    return trimmed.substr(0, stop + 1);
}

nlohmann::json makeTtsBag(const std::string& text)
{
    nlohmann::json bag = nlohmann::json::object();
    bag["enabled"] = true;
    bag["text"] = text;
    return bag;
}

std::string jobTypeLabel(ItemAiAssistJobType type)
{
    switch (type)
    {
    case ItemAiAssistJobType::GenerateImage:
        return "generate_image";
    case ItemAiAssistJobType::GenerateIcon:
        return "generate_icon";
    case ItemAiAssistJobType::GenerateExamineSound:
        return "generate_examine_sound";
    case ItemAiAssistJobType::GenerateUseSound:
        return "generate_use_sound";
    case ItemAiAssistJobType::GenerateConstructionDescription:
        return "generate_construction_description";
    case ItemAiAssistJobType::GenerateTtsConstructionDescription:
        return "generate_tts_construction_description";
    case ItemAiAssistJobType::GenerateTtsDescription:
        return "generate_tts_description";
    }
    return "unknown";
}

std::string summarizeAiPlan(const ItemAiAssistPlan& plan)
{
    if (plan.empty())
        return {};
    std::ostringstream stream;
    stream << "AI assist pending (" << plan.jobs.size() << "): ";
    for (size_t i = 0; i < plan.jobs.size(); ++i)
    {
        if (i > 0)
            stream << "; ";
        stream << jobTypeLabel(plan.jobs[i].type);
        if (!plan.jobs[i].outPath.empty())
            stream << " → " << plan.jobs[i].outPath;
    }
    return stream.str();
}

nlohmann::json componentLine(const std::string& itemId)
{
    nlohmann::json line = nlohmann::json::object();
    line["itemId"] = itemId;
    line["reqQty"] = 1;
    line["consume"] = true;
    return line;
}

void applyRecipeToItem(nlohmann::json& item, const ItemAuthoringPayload& payload)
{
    if (!payload.recipe.enabled)
    {
        item.erase("components");
        item.erase("assembleNarrative");
        item.erase("assembleTts");
        return;
    }

    // Advanced components JSON overrides the two-slot dropdowns when valid.
    bool usedAdvanced = false;
    if (!payload.recipe.advancedComponentsJson.empty())
    {
        try
        {
            nlohmann::json parsed =
                nlohmann::json::parse(payload.recipe.advancedComponentsJson);
            if (parsed.is_array())
            {
                item["components"] = parsed;
                usedAdvanced = true;
            }
        }
        catch (const nlohmann::json::exception&)
        {
            // Fall through to dropdown components.
        }
    }
    if (!usedAdvanced)
    {
        nlohmann::json components = nlohmann::json::array();
        if (!payload.recipe.component1.empty())
            components.push_back(componentLine(payload.recipe.component1));
        if (!payload.recipe.component2.empty()
            && payload.recipe.component2 != payload.recipe.component1)
            components.push_back(componentLine(payload.recipe.component2));
        item["components"] = components;
    }

    item["assembleNarrative"] = payload.recipe.constructionDescription;

    if (payload.recipe.ttsEnabled)
    {
        nlohmann::json bag = nlohmann::json::object();
        bag["enabled"] = !payload.recipe.ttsConstructionDescription.empty();
        bag["text"] = payload.recipe.ttsConstructionDescription;
        item["assembleTts"] = bag;
    }
    else
    {
        item.erase("assembleTts");
    }
}

void applyCapabilitiesToItem(nlohmann::json& item, const ItemAuthoringPayload& payload)
{
    const ItemCapabilitySwitches& cap = payload.capabilities;

    if (cap.stackable)
    {
        nlohmann::json quantity = item.value("quantity", nlohmann::json::object());
        if (!quantity.is_object())
            quantity = nlohmann::json::object();
        quantity["stackable"] = true;
        if (!quantity.contains("defaultQuantity"))
            quantity["defaultQuantity"] = 1;
        quantity["unitWeightLb"] = payload.weightLb;
        if (!quantity.contains("unitLabel"))
            quantity["unitLabel"] = "use";
        item["quantity"] = quantity;
    }
    else if (item.contains("quantity") && item["quantity"].is_object())
    {
        item["quantity"].erase("stackable");
        if (item["quantity"].empty()
            || (item["quantity"].size() == 1 && item["quantity"].contains("defaultQuantity")))
            item.erase("quantity");
    }

    if (cap.tool)
        item["consumeOnCombine"] = false;
    else
        item.erase("consumeOnCombine");

    if (!cap.consumeOnUse)
        item["consumeOnUse"] = false;
    else
        item.erase("consumeOnUse");

    if (cap.lightSource)
        item["lightSource"] = true;
    else
        item.erase("lightSource");

    // Item-local TTS gate follows the Description TTS switch.
    if (payload.descriptionTtsEnabled
        || payload.recipe.ttsEnabled
        || !payload.ttsDescription.empty())
    {
        item["ttsEnabled"] = true;
        if (!item.contains("ttsDefaultVoice")
            || !item["ttsDefaultVoice"].is_string()
            || item["ttsDefaultVoice"].get<std::string>().empty())
            item["ttsDefaultVoice"] = "leo";
    }
    else
    {
        item.erase("ttsEnabled");
    }
}

} // namespace

std::string slugifyItemId(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    bool lastUnderscore = false;
    for (unsigned char ch : name)
    {
        if (std::isalnum(ch))
        {
            out.push_back(static_cast<char>(std::tolower(ch)));
            lastUnderscore = false;
        }
        else if (ch == '\'' || ch == '`')
        {
            continue;
        }
        else if (!out.empty() && !lastUnderscore)
        {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

std::string defaultItemExamineImagePath(const std::string& itemId)
{
    return "resources/images/" + itemId + "_examine.png";
}

std::string defaultItemIconPath(const std::string& itemId)
{
    return "resources/icons/" + itemId + "_icon.png";
}

std::string defaultItemExamineSoundPath(const std::string& itemId)
{
    // Item SFX clips load as MP3 (see AudioManager::loadSoundClip).
    return "resources/audio/sfx/" + itemId + "_examine.mp3";
}

std::string defaultItemUseSoundPath(const std::string& itemId)
{
    return "resources/audio/sfx/" + itemId + "_use.mp3";
}

bool loadPayloadFromItemJson(
    const std::string& itemId,
    const nlohmann::json& item,
    ItemAuthoringPayload& out,
    std::string& errorOut)
{
    if (!item.is_object())
    {
        errorOut = "Item is not a JSON object.";
        return false;
    }
    out = ItemAuthoringPayload{};
    out.id = itemId;
    out.name = item.value("name", "");
    out.description = item.value("description", "");
    out.weightLb = item.value("weightLb", 0.0f);

    if (item.contains("visuals") && item["visuals"].is_object())
        out.imagePath = item["visuals"].value("image", "");
    if (item.contains("icons") && item["icons"].is_object())
        out.iconPath = item["icons"].value("icon", "");
    if (item.contains("sfx") && item["sfx"].is_object())
    {
        out.examineSoundPath = item["sfx"].value("examine", "");
        out.useSoundPath = item["sfx"].value("use", "");
    }
    if (item.contains("examineTts") && item["examineTts"].is_object())
    {
        out.ttsDescription = item["examineTts"].value(
            "text", item["examineTts"].value("ttsText", ""));
        out.descriptionTtsEnabled =
            item["examineTts"].value("enabled", item["examineTts"].value("tts", false))
            || !out.ttsDescription.empty();
    }
    // Also honor item-level TTS gate when examine text exists.
    if (item.value("ttsEnabled", false) && !out.ttsDescription.empty())
        out.descriptionTtsEnabled = true;

    out.capabilities.lightSource = item.value("lightSource", false);
    out.capabilities.tool = item.contains("consumeOnCombine")
        && item["consumeOnCombine"].is_boolean()
        && item["consumeOnCombine"].get<bool>() == false;
    out.capabilities.consumeOnUse = !(item.contains("consumeOnUse")
        && item["consumeOnUse"].is_boolean()
        && item["consumeOnUse"].get<bool>() == false);
    if (item.contains("quantity") && item["quantity"].is_object())
        out.capabilities.stackable = item["quantity"].value("stackable", false);

    if (item.contains("components") && item["components"].is_array()
        && !item["components"].empty())
    {
        out.recipe.enabled = true;
        const auto& comps = item["components"];
        if (comps.size() >= 1 && comps[0].is_object())
            out.recipe.component1 = comps[0].value("itemId", "");
        if (comps.size() >= 2 && comps[1].is_object())
            out.recipe.component2 = comps[1].value("itemId", "");
        out.recipe.advancedComponentsJson = comps.dump(2);
    }
    out.recipe.constructionDescription = item.value("assembleNarrative", "");
    if (item.contains("assembleTts") && item["assembleTts"].is_object())
    {
        out.recipe.ttsConstructionDescription =
            item["assembleTts"].value("text", item["assembleTts"].value("ttsText", ""));
        out.recipe.ttsEnabled =
            item["assembleTts"].value("enabled", item["assembleTts"].value("tts", false))
            || !out.recipe.ttsConstructionDescription.empty();
    }

    errorOut.clear();
    return true;
}

bool validateItemAuthoringPayload(
    const DocumentWorkspace& docs,
    ItemAuthoringPayload& payload,
    bool createOnly,
    std::string& errorOut)
{
    payload.name = trimCopy(payload.name);
    payload.description = trimCopy(payload.description);
    payload.id = trimCopy(payload.id);
    payload.recipe.component1 = trimCopy(payload.recipe.component1);
    payload.recipe.component2 = trimCopy(payload.recipe.component2);
    payload.imagePath = trimCopy(payload.imagePath);
    payload.iconPath = trimCopy(payload.iconPath);
    payload.examineSoundPath = trimCopy(payload.examineSoundPath);
    payload.useSoundPath = trimCopy(payload.useSoundPath);

    if (payload.name.empty())
    {
        errorOut = "Name is required.";
        return false;
    }
    if (payload.description.empty())
    {
        errorOut = "Description is required.";
        return false;
    }
    if (payload.weightLb < 0.0f)
    {
        errorOut = "Weight cannot be negative.";
        return false;
    }

    if (payload.id.empty())
        payload.id = slugifyItemId(payload.name);
    else if (createOnly)
        payload.id = slugifyItemId(payload.id);

    if (payload.id.empty())
    {
        errorOut = "Could not derive a valid item id from the name.";
        return false;
    }
    for (char ch : payload.id)
    {
        if (!(std::islower(static_cast<unsigned char>(ch))
              || std::isdigit(static_cast<unsigned char>(ch))
              || ch == '_'))
        {
            errorOut = "Item id must be snake_case (a-z, 0-9, underscore).";
            return false;
        }
    }

    if (!docs.itemsLoaded)
    {
        errorOut = "items.json is not loaded.";
        return false;
    }
    if (docs.itemsMap() == nullptr)
    {
        errorOut = "items.json has no items map.";
        return false;
    }

    if (createOnly && docs.itemJson(payload.id) != nullptr)
    {
        errorOut = "Item id already exists: " + payload.id;
        return false;
    }
    if (!createOnly && docs.itemJson(payload.id) == nullptr)
    {
        errorOut = "Item id not found: " + payload.id;
        return false;
    }

    if (payload.recipe.enabled)
    {
        if (!payload.recipe.advancedComponentsJson.empty())
        {
            try
            {
                const nlohmann::json parsed =
                    nlohmann::json::parse(payload.recipe.advancedComponentsJson);
                if (!parsed.is_array() || parsed.empty())
                {
                    errorOut = "Advanced components JSON must be a non-empty array.";
                    return false;
                }
            }
            catch (const nlohmann::json::exception& ex)
            {
                errorOut = std::string("Advanced components JSON: ") + ex.what();
                return false;
            }
        }
        else
        {
            if (payload.recipe.component1.empty() || payload.recipe.component2.empty())
            {
                errorOut = "Product Recipe requires Item 1 and Item 2.";
                return false;
            }
            if (payload.recipe.component1 == payload.recipe.component2)
            {
                errorOut = "Product Recipe components must be different items.";
                return false;
            }
            if (payload.recipe.component1 == payload.id
                || payload.recipe.component2 == payload.id)
            {
                errorOut = "A product cannot list itself as a craft component.";
                return false;
            }
            if (docs.itemJson(payload.recipe.component1) == nullptr)
            {
                errorOut = "Item 1 not found: " + payload.recipe.component1;
                return false;
            }
            if (docs.itemJson(payload.recipe.component2) == nullptr)
            {
                errorOut = "Item 2 not found: " + payload.recipe.component2;
                return false;
            }
        }
    }

    if (payload.imagePath.empty())
        payload.imagePath = defaultItemExamineImagePath(payload.id);
    if (payload.iconPath.empty())
        payload.iconPath = defaultItemIconPath(payload.id);

    errorOut.clear();
    return true;
}

nlohmann::json applyPayloadToItemJson(
    const ItemAuthoringPayload& payload,
    const nlohmann::json* existingOrNull)
{
    nlohmann::json item = nlohmann::json::object();
    if (existingOrNull != nullptr && existingOrNull->is_object())
        item = *existingOrNull;

    item["name"] = payload.name;
    item["description"] = payload.description;
    item["weightLb"] = payload.weightLb;

    if (!item.contains("visuals") || !item["visuals"].is_object())
        item["visuals"] = nlohmann::json::object();
    if (!payload.imagePath.empty())
        item["visuals"]["image"] = payload.imagePath;

    if (!item.contains("icons") || !item["icons"].is_object())
        item["icons"] = nlohmann::json::object();
    if (!payload.iconPath.empty())
        item["icons"]["icon"] = payload.iconPath;

    // SFX paths (examine / use sound).
    if (!payload.examineSoundPath.empty() || !payload.useSoundPath.empty())
    {
        if (!item.contains("sfx") || !item["sfx"].is_object())
            item["sfx"] = nlohmann::json::object();
        if (!payload.examineSoundPath.empty())
            item["sfx"]["examine"] = payload.examineSoundPath;
        if (!payload.useSoundPath.empty())
            item["sfx"]["use"] = payload.useSoundPath;
    }

    applyCapabilitiesToItem(item, payload);
    applyRecipeToItem(item, payload);

    if (payload.descriptionTtsEnabled)
    {
        std::string text = payload.ttsDescription;
        if (text.empty() && payload.aiAssist.assistTtsDescription)
            text = payload.description;
        if (!text.empty())
            item["examineTts"] = makeTtsBag(text);
        else
        {
            nlohmann::json bag = nlohmann::json::object();
            bag["enabled"] = true;
            bag["text"] = "";
            item["examineTts"] = bag;
        }
    }

    return item;
}

ItemAiAssistPlan planItemAiAssist(const ItemAuthoringPayload& payload)
{
    ItemAiAssistPlan plan;
    const std::string& desc = payload.description;
    const std::string styleHint =
        "Period western mountain-ridge adventure game, grounded physical prop, "
        "no UI chrome, no text watermark.";

    if (payload.aiAssist.generateImageFromDescription)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateImage;
        job.outPath = payload.imagePath.empty()
            ? defaultItemExamineImagePath(payload.id)
            : payload.imagePath;
        job.prompt =
            "Generate a full-screen examine image for inventory item \""
            + payload.name + "\". Description: " + desc + " " + styleHint;
        plan.jobs.push_back(job);
    }
    if (payload.aiAssist.generateIconFromDescription)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateIcon;
        job.outPath = payload.iconPath.empty()
            ? defaultItemIconPath(payload.id)
            : payload.iconPath;
        job.prompt =
            "Generate a square inventory icon for item \"" + payload.name
            + "\". Description: " + desc
            + " Clean centered subject, readable at small size. " + styleHint;
        plan.jobs.push_back(job);
    }
    if (payload.aiAssist.generateExamineSound)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateExamineSound;
        job.action = "examine";
        job.outPath = payload.examineSoundPath.empty()
            ? defaultItemExamineSoundPath(payload.id)
            : payload.examineSoundPath;
        job.prompt =
            "Generate examine SFX for item \"" + payload.name
            + "\" from: " + desc;
        plan.jobs.push_back(job);
    }
    if (payload.aiAssist.generateUseSound)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateUseSound;
        job.action = "use";
        job.outPath = payload.useSoundPath.empty()
            ? defaultItemUseSoundPath(payload.id)
            : payload.useSoundPath;
        job.prompt =
            "Generate use SFX for item \"" + payload.name + "\" from: " + desc;
        plan.jobs.push_back(job);
    }
    if (payload.aiAssist.assistConstructionDescription)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateConstructionDescription;
        job.outPath = "assembleNarrative";
        job.prompt =
            "Write construction/combine narrative for crafting product \""
            + payload.name + "\" from components \"" + payload.recipe.component1
            + "\" + \"" + payload.recipe.component2 + "\". Item description: "
            + desc;
        plan.jobs.push_back(job);
    }
    if (payload.aiAssist.assistTtsConstructionDescription)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateTtsConstructionDescription;
        job.outPath = "assembleTts";
        job.action = "assemble";
        job.prompt =
            "Write spoken assemble TTS for crafting \"" + payload.name
            + "\". Construction text: " + payload.recipe.constructionDescription
            + " Description: " + desc;
        plan.jobs.push_back(job);
    }
    if (payload.aiAssist.assistTtsDescription)
    {
        ItemAiAssistJob job;
        job.type = ItemAiAssistJobType::GenerateTtsDescription;
        job.outPath = "examineTts";
        job.action = "examine";
        job.prompt =
            "Write spoken examine TTS for item \"" + payload.name
            + "\" from description: " + desc;
        plan.jobs.push_back(job);
    }
    return plan;
}

// Forward declare resolve helper used below (defined later in this file).
static std::string resolveGameRootForAuthoring(const std::string& assetRoot);

bool writeItemAiAssistJobsFile(
    const std::string& assetRoot,
    const std::string& itemId,
    const ItemAiAssistPlan& plan,
    std::string& errorOut)
{
    if (plan.empty())
    {
        errorOut.clear();
        return true;
    }
    if (itemId.empty())
    {
        errorOut = "Missing item id for AI jobs file.";
        return false;
    }

    // Prefer real game root (handles scene-editor build/ symlink layout).
    const std::string gameRoot = resolveGameRootForAuthoring(assetRoot);
    const std::string authoringDir = pathJoin(gameRoot, "resources/.authoring");
    if (!ensureDirectory(authoringDir))
    {
        errorOut = "Failed to create authoring directory:\n" + authoringDir;
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    root["itemId"] = itemId;
    root["jobs"] = nlohmann::json::array();
    for (const ItemAiAssistJob& job : plan.jobs)
    {
        nlohmann::json entry = nlohmann::json::object();
        entry["type"] = jobTypeLabel(job.type);
        entry["prompt"] = job.prompt;
        entry["outPath"] = job.outPath;
        if (!job.action.empty())
            entry["action"] = job.action;
        root["jobs"].push_back(entry);
    }

    const std::string path = pathJoin(authoringDir, itemId + "_ai_jobs.json");
    std::ofstream out(path.c_str());
    if (!out.is_open())
    {
        errorOut = "Failed to write AI jobs file:\n" + path;
        return false;
    }
    out << root.dump(2) << '\n';
    if (!out.good())
    {
        errorOut = "Failed while writing AI jobs file:\n" + path;
        return false;
    }
    errorOut.clear();
    return true;
}

ItemAuthoringResult upsertItemFromPayload(
    DocumentWorkspace& docs,
    ItemAuthoringPayload payload,
    bool createOnly)
{
    ItemAuthoringResult result;
    if (!validateItemAuthoringPayload(docs, payload, createOnly, result.error))
    {
        result.ok = false;
        return result;
    }

    nlohmann::json* map = docs.itemsMap();
    if (map == nullptr)
    {
        result.error = "items.json has no items map.";
        result.ok = false;
        return result;
    }

    if (payload.aiAssist.assistConstructionDescription
        && payload.recipe.constructionDescription.empty())
    {
        payload.recipe.constructionDescription =
            "You combine the parts into " + payload.name + ". "
            + firstSentence(payload.description);
    }
    if (payload.aiAssist.assistTtsConstructionDescription
        && payload.recipe.ttsConstructionDescription.empty())
    {
        payload.recipe.ttsConstructionDescription =
            !payload.recipe.constructionDescription.empty()
            ? payload.recipe.constructionDescription
            : ("You assemble the " + payload.name + ".");
    }
    if (payload.aiAssist.assistTtsDescription && payload.ttsDescription.empty())
        payload.ttsDescription = payload.description;

    const nlohmann::json* existing = createOnly ? nullptr : docs.itemJson(payload.id);
    nlohmann::json itemJson = applyPayloadToItemJson(payload, existing);
    (*map)[payload.id] = std::move(itemJson);
    docs.markDirty();

    result.ok = true;
    result.itemId = payload.id;
    result.aiPlan = planItemAiAssist(payload);
    result.aiStatus = summarizeAiPlan(result.aiPlan);

    if (!result.aiPlan.empty())
    {
        std::string jobsError;
        if (!writeItemAiAssistJobsFile(
                docs.assetRoot, payload.id, result.aiPlan, jobsError)
            && !jobsError.empty())
        {
            if (!result.aiStatus.empty())
                result.aiStatus += " | ";
            result.aiStatus += jobsError;
        }
        else if (!result.aiStatus.empty())
        {
            result.aiStatus +=
                " | jobs: resources/.authoring/" + payload.id + "_ai_jobs.json";
        }
    }

    result.error.clear();
    return result;
}

namespace
{

bool fileLooksReadable(const std::string& path)
{
    std::ifstream in(path.c_str());
    return in.good();
}

std::string parentPath(const std::string& path)
{
    if (path.empty())
        return {};
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
        p.pop_back();
    const auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return p.substr(0, slash);
}

/** Locate game root (contains tools/run_item_authoring_ai.py and resources/). */
std::string resolveGameRoot(const std::string& assetRoot)
{
    std::string cursor = assetRoot.empty() ? "." : assetRoot;
    for (int i = 0; i < 6; ++i)
    {
        const std::string script =
            pathJoin(cursor, "tools/run_item_authoring_ai.py");
        const std::string items =
            pathJoin(cursor, "resources/items.json");
        if (fileLooksReadable(script) && fileLooksReadable(items))
            return cursor;
        const std::string up = parentPath(cursor);
        if (up == cursor)
            break;
        cursor = up;
    }
    return assetRoot;
}

/** Shell-escape a value with single quotes (for optional --key=). */
std::string shellSingleQuote(const std::string& value)
{
    std::string out = "'";
    for (char ch : value)
    {
        if (ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

} // namespace

// Used by writeItemAiAssistJobsFile (defined earlier in this file).
static std::string resolveGameRootForAuthoring(const std::string& assetRoot)
{
    // Call the same logic as the anonymous-namespace resolveGameRoot via
    // runItemAuthoringAiJobs's resolution path — duplicate walk here so
    // write jobs land next to resources/ even when editor assetRoot is build/.
    auto fileLooksReadableLocal = [](const std::string& path) -> bool {
        std::ifstream in(path.c_str());
        return in.good();
    };
    auto parentPathLocal = [](const std::string& path) -> std::string {
        if (path.empty())
            return {};
        std::string p = path;
        while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
            p.pop_back();
        const auto slash = p.find_last_of("/\\");
        if (slash == std::string::npos)
            return ".";
        if (slash == 0)
            return "/";
        return p.substr(0, slash);
    };
    std::string cursor = assetRoot.empty() ? "." : assetRoot;
    for (int i = 0; i < 6; ++i)
    {
        const std::string script =
            pathJoin(cursor, "tools/run_item_authoring_ai.py");
        const std::string items =
            pathJoin(cursor, "resources/items.json");
        if (fileLooksReadableLocal(script) && fileLooksReadableLocal(items))
            return cursor;
        const std::string up = parentPathLocal(cursor);
        if (up == cursor)
            break;
        cursor = up;
    }
    return assetRoot;
}

bool runItemAuthoringAiJobs(
    const std::string& assetRoot,
    const std::string& itemId,
    std::string& statusOut,
    const std::string& apiKey)
{
    if (itemId.empty())
    {
        statusOut = "Missing item id for AI job runner.";
        return false;
    }

    const std::string gameRoot = resolveGameRootForAuthoring(assetRoot);
    const std::string jobsFile =
        pathJoin(gameRoot, "resources/.authoring/" + itemId + "_ai_jobs.json");
    const std::string script =
        pathJoin(gameRoot, "tools/run_item_authoring_ai.py");
    const std::string logFile =
        pathJoin(gameRoot, "resources/.authoring/" + itemId + "_ai_run.log");

    if (!fileLooksReadable(script))
    {
        statusOut =
            "AI job runner script not found. Expected:\n" + script;
        return false;
    }
    if (!fileLooksReadable(jobsFile))
    {
        statusOut = "AI jobs file not found:\n" + jobsFile;
        return false;
    }

    // Append a C++ preamble to the log so the full pipeline is detailed.
    {
        std::ofstream logPre(logFile.c_str(), std::ios::trunc);
        if (logPre)
        {
            logPre << "=== Timberline authoring AI runner ===\n"
                   << "itemId: " << itemId << "\n"
                   << "gameRoot: " << gameRoot << "\n"
                   << "jobsFile: " << jobsFile << "\n"
                   << "script: " << script << "\n"
                   << "hasSessionKey: " << (apiKey.empty() ? "no" : "yes") << "\n"
                   << "--- python output follows ---\n";
        }
        std::cerr << "TIMBERLINE authoring: launching runner item=" << itemId
                  << " root=" << gameRoot
                  << " key=" << (apiKey.empty() ? "no" : "yes") << "\n";
    }

    // Redirect runner output (append) to the log the UI can surface.
    // Session API key is passed only via CLI --key (not written to disk).
    auto runWith = [&](const char* pythonBin) -> int {
        std::ostringstream command;
        command << pythonBin << " \"" << script << "\" --asset-root \"" << gameRoot
                << "\" --jobs-file \"" << jobsFile << "\"";
        if (!apiKey.empty())
            command << " --key " << shellSingleQuote(apiKey);
        command << " >> \"" << logFile << "\" 2>&1";
        std::cerr << "TIMBERLINE authoring: exec " << pythonBin << "\n";
        return std::system(command.str().c_str());
    };

    int code = runWith("python3");
    if (code != 0)
    {
        std::cerr << "TIMBERLINE authoring: python3 exit " << code
                  << ", trying python\n";
        code = runWith("python");
    }
    std::cerr << "TIMBERLINE authoring: runner exit code " << code << "\n";

    // Prefer structured errors from the jobs file lastRun block.
    std::string structuredErrors;
    std::string producedSummary;
    try
    {
        std::ifstream jobsIn(jobsFile.c_str());
        if (jobsIn)
        {
            nlohmann::json root;
            jobsIn >> root;
            if (root.contains("lastRun") && root["lastRun"].is_object())
            {
                const auto& last = root["lastRun"];
                if (last.contains("errors") && last["errors"].is_array())
                {
                    for (const auto& err : last["errors"])
                    {
                        if (!err.is_string())
                            continue;
                        if (!structuredErrors.empty())
                            structuredErrors += " | ";
                        structuredErrors += err.get<std::string>();
                    }
                }
                if (last.contains("produced") && last["produced"].is_array())
                {
                    for (const auto& p : last["produced"])
                    {
                        if (!p.is_string())
                            continue;
                        if (!producedSummary.empty())
                            producedSummary += ", ";
                        producedSummary += p.get<std::string>();
                    }
                }
            }
        }
    }
    catch (const nlohmann::json::exception&)
    {
    }

    // Tail the log if present (last ~600 chars) for debugging.
    std::string logTail;
    {
        std::ifstream logIn(logFile.c_str());
        if (logIn)
        {
            std::ostringstream ss;
            ss << logIn.rdbuf();
            logTail = ss.str();
            if (logTail.size() > 600)
                logTail = logTail.substr(logTail.size() - 600);
        }
    }

    if (code != 0 || !structuredErrors.empty())
    {
        statusOut = "AI asset generation incomplete";
        if (!producedSummary.empty())
            statusOut += " (wrote: " + producedSummary + ")";
        if (!structuredErrors.empty())
            statusOut += ". " + structuredErrors;
        else
            statusOut +=
                ". Runner exit " + std::to_string(code)
                + ". Paste an xAI API key in the AI Assist section for images; "
                  "install lame/ffmpeg for SFX.";
        if (!logTail.empty())
            statusOut += "\n--- log ---\n" + logTail;
        return false;
    }

    statusOut =
        "AI assets generated for " + itemId;
    if (!producedSummary.empty())
        statusOut += ": " + producedSummary;
    return true;
}

} // namespace timberline_editor
