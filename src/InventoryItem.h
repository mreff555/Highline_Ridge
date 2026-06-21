#ifndef INVENTORY_ITEM_H
#define INVENTORY_ITEM_H

#include <ItemInstance.h>
#include <raylib.h>
#include <string>

namespace testgame
{

struct InventoryItem
{
    ItemInstance instance;
    std::string id;
    std::string name;
    std::string iconPath;
    std::string examineImagePath;
    Texture2D icon{};
    Texture2D examineImage{};
    std::string examineText;
    float weightLb = 0.0f;
};

}

#endif /* INVENTORY_ITEM_H */