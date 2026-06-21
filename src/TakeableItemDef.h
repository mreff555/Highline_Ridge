#ifndef TAKEABLE_ITEM_DEF_H
#define TAKEABLE_ITEM_DEF_H

#include <string>

namespace testgame
{

struct TakeableItemDef
{
    std::string id;
    std::string name;
    std::string iconPath;
    std::string examineImagePath;
    std::string examineText;
    bool requiresExamine = true;

    bool hasOverrides() const
    {
        return !name.empty()
            || !iconPath.empty()
            || !examineImagePath.empty()
            || !examineText.empty();
    }
};

}

#endif /* TAKEABLE_ITEM_DEF_H */