#ifndef ITEM_DEF_H
#define ITEM_DEF_H

#include <AudioTypes.h>
#include <string>
#include <vector>

namespace testgame
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