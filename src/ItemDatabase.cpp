#include "ItemDatabase.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace testgame
{

ItemDatabase* ItemDatabase::activeDatabase = nullptr;

namespace
{

bool parseAudioClipDef(
    const nlohmann::json& clip,
    AudioClipDef& out,
    bool defaultLoop)
{
    if (!clip.is_object())
        return false;

    out.path = clip.value("path", clip.value("file", ""));
    if (out.path.empty())
        return false;

    out.volume = clip.value("volume", 1.0f);
    out.fadeIn = clip.value("fade_in", clip.value("fadeIn", 0.0f));
    out.fadeOut = clip.value("fade_out", clip.value("fadeOut", 0.0f));
    out.loop = clip.value("loop", defaultLoop);
    out.trigger.clear();
    out.numericAttributes.clear();
    out.boolAttributes.clear();
    out.stringAttributes.clear();
    return true;
}

bool parseItemTts(const nlohmann::json& json, ItemTtsDef& out)
{
    if (!json.is_object())
        return true;

    out.enabled = json.value("tts", json.value("enabled", false));
    out.voice = json.value("ttsVoice", json.value("voice", ""));
    out.text = json.value("ttsText", json.value("text", ""));
    out.audio = json.value("ttsAudio", json.value("audio", ""));
    return true;
}

bool parseItemSfx(const nlohmann::json& json, ItemSfxDef& out)
{
    if (!json.is_object())
        return true;

    out.open = json.value("open", "");
    out.close = json.value("close", "");
    out.examine = json.value("examine", "");
    out.use = json.value("use", "");
    return true;
}

bool parseItemAudioOverlay(const nlohmann::json& json, ItemAudioOverlayDef& out)
{
    if (!json.is_object())
        return true;

    out.sceneMix = json.value("sceneMix", json.value("scene_mix", 1.0f));
    out.muteSceneAudio = json.value("muteSceneAudio", json.value("mute_scene_audio", false));

    const nlohmann::json& music = json.value("music", nlohmann::json::object());
    if (music.is_object() && !music.empty())
    {
        AudioClipDef musicClip;
        if (parseAudioClipDef(music, musicClip, true))
        {
            out.music = musicClip;
            out.hasMusic = true;
        }
    }

    out.ambient.clear();
    const nlohmann::json& ambient = json.value("ambient", nlohmann::json::array());
    if (ambient.is_array())
    {
        for (const nlohmann::json& clip : ambient)
        {
            AudioClipDef parsed;
            if (parseAudioClipDef(clip, parsed, true))
                out.ambient.push_back(parsed);
        }
    }

    return true;
}

bool parseContainerContent(const nlohmann::json& json, ItemContainerContentDef& out)
{
    if (!json.is_object())
        return false;

    out.itemId = json.value("itemId", json.value("id", ""));
    out.quantity = json.value("quantity", 1);
    out.hidden = json.value("hidden", false);
    out.revealFlag = json.value("revealFlag", json.value("reveal_flag", ""));
    return !out.itemId.empty();
}

bool parseItemContainer(const nlohmann::json& json, ItemContainerDef& out)
{
    if (!json.is_object())
        return true;

    out.isContainer = json.value("isContainer", json.value("is_container", false));
    out.decomposes = json.value("decomposes", false);
    out.contents.clear();

    const nlohmann::json& contents = json.value("contents", nlohmann::json::array());
    if (!contents.is_array())
        return true;

    for (const nlohmann::json& entry : contents)
    {
        ItemContainerContentDef parsed;
        if (parseContainerContent(entry, parsed))
            out.contents.push_back(parsed);
    }

    return true;
}

bool parseItemQuantity(const nlohmann::json& json, ItemQuantityDef& out)
{
    if (!json.is_object())
        return true;

    out.stackable = json.value("stackable", false);
    out.defaultQuantity = json.value("defaultQuantity", json.value("default_quantity", 1));
    out.unitWeightLb = json.value("unitWeightLb", json.value("unit_weight_lb", 0.0f));
    out.unitLabel = json.value("unitLabel", json.value("unit_label", ""));
    return true;
}

bool parseItemDef(const std::string& id, const nlohmann::json& json, ItemDef& out)
{
    if (!json.is_object())
        return false;

    out.id = id;
    out.name = json.value("name", "");
    out.description = json.value("description", json.value("examineText", ""));
    out.weightLb = json.value("weightLb", json.value("weight_lb", 0.0f));

    const nlohmann::json& visuals = json.value("visuals", nlohmann::json::object());
    if (visuals.is_object() && !visuals.empty())
    {
        out.visuals.image = visuals.value("image", "");
        out.visuals.alternateImage = visuals.value("alternateImage", visuals.value("alternate_image", ""));
        out.visuals.alternateImageFlag = visuals.value(
            "alternateImageFlag",
            visuals.value("alternate_image_flag", ""));
    }
    else
    {
        out.visuals.image = json.value("image", json.value("examineImage", ""));
        out.visuals.alternateImage = json.value("alternateImage", json.value("alternate_image", ""));
        out.visuals.alternateImageFlag = json.value(
            "alternateImageFlag",
            json.value("alternate_image_flag", ""));
    }

    const nlohmann::json& icons = json.value("icons", nlohmann::json::object());
    if (icons.is_object() && !icons.empty())
    {
        out.icons.icon = icons.value("icon", "");
        out.icons.alternateIcon = icons.value("alternateIcon", icons.value("alternate_icon", ""));
        out.icons.alternateIconFlag = icons.value(
            "alternateIconFlag",
            icons.value("alternate_icon_flag", ""));
    }
    else
    {
        out.icons.icon = json.value("icon", "");
        out.icons.alternateIcon = json.value("alternateIcon", json.value("alternate_icon", ""));
        out.icons.alternateIconFlag = json.value(
            "alternateIconFlag",
            json.value("alternate_icon_flag", ""));
    }

    parseItemTts(json.value("examineTts", json.value("examine_tts", nlohmann::json::object())), out.examineTts);
    parseItemSfx(json.value("sfx", nlohmann::json::object()), out.sfx);
    parseItemAudioOverlay(json.value("examineAudio", json.value("examine_audio", nlohmann::json::object())), out.examineAudio);
    parseItemContainer(json.value("container", nlohmann::json::object()), out.container);
    parseItemQuantity(json.value("quantity", nlohmann::json::object()), out.quantity);

    return !out.id.empty() && !out.name.empty();
}

bool shouldIncludeContainerContent(
    const ItemContainerContentDef& content,
    const std::vector<std::string>& storyFlags)
{
    if (!content.hidden)
        return true;
    if (content.revealFlag.empty())
        return false;
    return hasItemFlag(storyFlags, content.revealFlag);
}

void appendStoryFlags(std::vector<std::string>& outFlags, const std::vector<std::string>& storyFlags)
{
    for (const std::string& flag : storyFlags)
    {
        if (flag.empty())
            continue;
        if (!hasItemFlag(outFlags, flag))
            outFlags.push_back(flag);
    }
}

}

bool ItemDatabase::load(const std::string& path, const std::string& assetRoot)
{
    (void)assetRoot;

    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    items.clear();

    const nlohmann::json& itemMap = root.value("items", nlohmann::json::object());
    if (!itemMap.is_object())
        return false;

    for (auto it = itemMap.begin(); it != itemMap.end(); ++it)
    {
        ItemDef parsed;
        if (!parseItemDef(it.key(), it.value(), parsed))
            return false;
        items[it.key()] = parsed;
    }

    activeDatabase = this;
    return !items.empty();
}

const ItemDef* ItemDatabase::getDef(const std::string& id) const
{
    std::map<std::string, ItemDef>::const_iterator it = items.find(id);
    if (it == items.end())
        return nullptr;
    return &it->second;
}

bool ItemDatabase::hasDef(const std::string& id) const
{
    return items.find(id) != items.end();
}

ItemInstance ItemDatabase::createInstance(
    const std::string& defId,
    const std::vector<std::string>& storyFlags) const
{
    ItemInstance instance;
    instance.instanceId = defId;
    instance.defId = defId;

    const ItemDef* def = getDef(defId);
    if (def == nullptr)
        return instance;

    if (def->quantity.stackable)
        instance.quantity = std::max(1, def->quantity.defaultQuantity);
    else
        instance.quantity = 1;

    appendStoryFlags(instance.activeFlags, storyFlags);

    if (def->container.isContainer)
    {
        for (const ItemContainerContentDef& content : def->container.contents)
        {
            if (!shouldIncludeContainerContent(content, storyFlags))
                continue;

            ItemInstance child = createInstance(content.itemId, storyFlags);
            child.quantity = std::max(1, content.quantity);
            instance.contents.push_back(child);
        }
    }

    return instance;
}

ItemInstance ItemDatabase::createInstanceFromOverrides(
    const std::string& defId,
    const ItemDefOverrides& overrides,
    const std::vector<std::string>& storyFlags) const
{
    ItemInstance instance = createInstance(defId, storyFlags);
    (void)overrides;
    return instance;
}

std::string ItemDatabase::resolveName(
    const ItemDef& def,
    const ItemDefOverrides& overrides) const
{
    if (!overrides.name.empty())
        return overrides.name;
    return def.name;
}

std::string ItemDatabase::resolveDescription(
    const ItemDef& def,
    const ItemDefOverrides& overrides) const
{
    if (!overrides.description.empty())
        return overrides.description;
    return def.description;
}

std::string ItemDatabase::resolveIconPath(const ItemDef& def, const ItemInstance& instance) const
{
    return resolveItemPath(
        def.icons.icon,
        def.icons.alternateIcon,
        def.icons.alternateIconFlag,
        instance.activeFlags);
}

std::string ItemDatabase::resolveImagePath(const ItemDef& def, const ItemInstance& instance) const
{
    return resolveItemPath(
        def.visuals.image,
        def.visuals.alternateImage,
        def.visuals.alternateImageFlag,
        instance.activeFlags);
}

float ItemDatabase::resolveWeightLb(const ItemDef& def, const ItemInstance& instance) const
{
    return computeContainerTotalWeightLb(def, instance, &ItemDatabase::resolveDefCallback);
}

const ItemDef* ItemDatabase::resolveDefCallback(const std::string& defId)
{
    if (activeDatabase == nullptr)
        return nullptr;
    return activeDatabase->getDef(defId);
}

InventoryItem ItemDatabase::buildInventoryItem(
    const ItemInstance& instance,
    const ItemDefOverrides& overrides) const
{
    InventoryItem item;
    item.instance = instance;
    item.id = instance.defId.empty() ? instance.instanceId : instance.defId;

    const ItemDef* def = getDef(item.id);
    if (def != nullptr)
    {
        item.name = resolveName(*def, overrides);
        item.examineText = resolveDescription(*def, overrides);
        item.iconPath = !overrides.iconPath.empty()
            ? overrides.iconPath
            : resolveIconPath(*def, instance);
        item.examineImagePath = !overrides.examineImagePath.empty()
            ? overrides.examineImagePath
            : resolveImagePath(*def, instance);
        item.weightLb = resolveWeightLb(*def, instance);
        return item;
    }

    item.name = overrides.name.empty() ? item.id : overrides.name;
    item.examineText = overrides.description;
    item.iconPath = overrides.iconPath;
    item.examineImagePath = overrides.examineImagePath;
    return item;
}

}