/*******************************************************************************
 * Timberline engine — period music style catalog for Edit Scene.
 * Ids must match tools/run_item_authoring_ai.py MUSIC_STYLE_PRESETS.
 ******************************************************************************/

#ifndef TIMBERLINE_MUSIC_STYLE_PRESETS_H
#define TIMBERLINE_MUSIC_STYLE_PRESETS_H

#include <cstring>
#include <string>

namespace timberline_editor
{

struct MusicStylePreset
{
    const char* id;
    const char* label; // succinct UI label
};

/** Ordered list shown in the Music style dropdown. */
inline constexpr MusicStylePreset kMusicStylePresets[] = {
    {"saloon_piano", "Saloon piano"},
    {"parlor_waltz", "Parlor waltz"},
    {"dance_hall", "Dance hall"},
    {"trail_folk", "Trail fiddle"},
    {"cabin_hearth", "Cabin piano"},
    {"hotel_parlor", "Hotel parlor"},
    {"mining_camp", "Camp harmonica"},
    {"depot_guitar", "Depot guitar"},
    {"river_guitar", "River guitar"},
    {"night_watch", "Night watch"},
    {"snowbound", "Snowbound"},
    {"tension", "Tension"},
    {"quiet_inquiry", "Quiet inquiry"},
    {"vespers", "Vespers"},
    {"title_hymn", "Title hymn"},
    {"scarlet_whispers", "Scarlet violin"},
};

inline constexpr int kMusicStylePresetCount =
    static_cast<int>(sizeof(kMusicStylePresets) / sizeof(kMusicStylePresets[0]));

inline int musicStylePresetIndex(const std::string& id)
{
    for (int i = 0; i < kMusicStylePresetCount; ++i)
    {
        if (id == kMusicStylePresets[i].id)
            return i;
    }
    // Default: cabin piano
    for (int i = 0; i < kMusicStylePresetCount; ++i)
    {
        if (std::strcmp(kMusicStylePresets[i].id, "cabin_hearth") == 0)
            return i;
    }
    return 0;
}

inline const char* musicStylePresetLabel(const std::string& id)
{
    return kMusicStylePresets[musicStylePresetIndex(id)].label;
}

inline const char* musicStylePresetIdAt(int index)
{
    if (index < 0 || index >= kMusicStylePresetCount)
        return kMusicStylePresets[0].id;
    return kMusicStylePresets[index].id;
}

} // namespace timberline_editor

#endif
