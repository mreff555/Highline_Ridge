/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations flowchart: shared node kinds + port layout helpers (Phase 1 shell).
 ******************************************************************************/

#ifndef TIMBERLINE_DIALOG_FLOW_TYPES_H
#define TIMBERLINE_DIALOG_FLOW_TYPES_H

#include <raylib.h>
#include <string>

namespace timberline_editor
{

/** Palette / canvas node kinds (Phase 1 stubs). */
enum class DialogNodeKind
{
    ActorDialog = 0,
    PlayerDialog,
    Event,
    GetItem,
    Attack,
    TriggerEvent,
    ActorInventory,
    Count
};

inline const char* dialogNodeKindLabel(DialogNodeKind kind)
{
    switch (kind)
    {
    case DialogNodeKind::ActorDialog:
        return "Actor Dialog";
    case DialogNodeKind::PlayerDialog:
        return "Player Dialog";
    case DialogNodeKind::Event:
        return "Event";
    case DialogNodeKind::GetItem:
        return "Get item";
    case DialogNodeKind::Attack:
        return "Attack";
    case DialogNodeKind::TriggerEvent:
        return "Trigger event";
    case DialogNodeKind::ActorInventory:
        return "Actor inventory";
    default:
        return "Node";
    }
}

inline const char* dialogNodeKindBrief(DialogNodeKind kind)
{
    switch (kind)
    {
    case DialogNodeKind::ActorDialog:
        return "NPC line (5 outs)";
    case DialogNodeKind::PlayerDialog:
        return "Player line (1 out)";
    case DialogNodeKind::Event:
        return "Gate (pass / fail)";
    case DialogNodeKind::GetItem:
        return "Grant inventory item";
    case DialogNodeKind::Attack:
        return "Fight (win/lose)";
    case DialogNodeKind::TriggerEvent:
        return "Fire story event";
    case DialogNodeKind::ActorInventory:
        return "Priced stock browse/buy";
    default:
        return "";
    }
}

/** Short badge letter drawn on stub icons. */
inline const char* dialogNodeKindGlyph(DialogNodeKind kind)
{
    switch (kind)
    {
    case DialogNodeKind::ActorDialog:
        return "A";
    case DialogNodeKind::PlayerDialog:
        return "P";
    case DialogNodeKind::Event:
        return "E";
    case DialogNodeKind::GetItem:
        return "I";
    case DialogNodeKind::Attack:
        return "X";
    case DialogNodeKind::TriggerEvent:
        return "T";
    case DialogNodeKind::ActorInventory:
        return "$";
    default:
        return "?";
    }
}

inline int dialogNodeChildCount(DialogNodeKind kind)
{
    switch (kind)
    {
    case DialogNodeKind::ActorDialog:
        return 5;
    case DialogNodeKind::PlayerDialog:
        return 1;
    case DialogNodeKind::Event:
        return 2; // pass (0) / fail (1) — e.g. money gate
    case DialogNodeKind::GetItem:
        return 1;
    case DialogNodeKind::Attack:
        return 2;
    case DialogNodeKind::TriggerEvent:
        return 1;
    case DialogNodeKind::ActorInventory:
        return 1; // after-browse continue (look-again is runtime-internal)
    default:
        return 0;
    }
}

/** Filename under resources/ui/editor/dialog_nodes/ */
inline const char* dialogNodeKindIconFile(DialogNodeKind kind)
{
    switch (kind)
    {
    case DialogNodeKind::ActorDialog:
        return "actor_dialog.png";
    case DialogNodeKind::PlayerDialog:
        return "player_dialog.png";
    case DialogNodeKind::Event:
        return "event.png";
    case DialogNodeKind::GetItem:
        return "get_item.png";
    case DialogNodeKind::Attack:
        return "attack.png";
    case DialogNodeKind::TriggerEvent:
        return "trigger_event.png";
    case DialogNodeKind::ActorInventory:
        return "actor_inventory.png";
    default:
        return "";
    }
}

/** Parent (in) port color — top center. */
inline constexpr Color kDialogParentPort = {90, 160, 200, 255};
/** Child (out) port color — bottom / sides. */
inline constexpr Color kDialogChildPort = {210, 120, 90, 255};
/** Speak-graph wire (not gold MOVE / silver Use). */
inline constexpr Color kDialogFlowWire = {200, 90, 100, 230};

/**
 * Child port slots 0..n-1 filled from bottom-center outward:
 * 0 = bottom, then SW, SE, left, right (max 5).
 */
enum class DialogChildSlot
{
    Bottom = 0,
    BottomLeft,
    BottomRight,
    Left,
    Right
};

inline DialogChildSlot dialogChildSlotForIndex(int index)
{
    switch (index)
    {
    case 0:
        return DialogChildSlot::Bottom;
    case 1:
        return DialogChildSlot::BottomLeft;
    case 2:
        return DialogChildSlot::BottomRight;
    case 3:
        return DialogChildSlot::Left;
    case 4:
        return DialogChildSlot::Right;
    default:
        return DialogChildSlot::Bottom;
    }
}

/**
 * Stat modifier sketch (Phase 2). Documented for Attack awards etc.
 * Modes: deltaValue | deltaPercent (of current) | setPercent (of max).
 * Stats: health, energy, resolve, lucidity, charisma.
 */
struct DialogStatModifier
{
    std::string stat; // "health" | "energy" | ...
    std::string mode; // "deltaValue" | "deltaPercent" | "setPercent"
    float amount = 0.0f;
};

} // namespace timberline_editor

#endif
