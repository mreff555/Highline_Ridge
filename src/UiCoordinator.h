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

#ifndef UI_COORDINATOR_H
#define UI_COORDINATOR_H

namespace timberline_engine
{

class DropConfirmMgr;
class InteractionMgr;
class SpeakTargetMgr;
class InventoryMgr;
class PauseMenuMgr;
class SaveLoadMenuMgr;
class TakeMgr;

enum class UiMode
{
    Gameplay,
    Inventory,
    Take,
    Interact,
    Speak,
    DropConfirm,
    Pause,
    SaveLoad
};

class UiCoordinator
{
    public:
    UiMode getMode() const { return mode; }
    bool isGameplay() const { return mode == UiMode::Gameplay; }

    void open(UiMode newMode,
        InventoryMgr& inventoryMgr,
        TakeMgr& takeMgr,
        InteractionMgr& interactionMgr,
        SpeakTargetMgr& speakTargetMgr,
        DropConfirmMgr& dropConfirmMgr,
        PauseMenuMgr& pauseMenu,
        SaveLoadMenuMgr& saveLoadMenu);

    void openSaveMenu(SaveLoadMenuMgr& saveLoadMenu);
    void openLoadMenu(SaveLoadMenuMgr& saveLoadMenu);
    void openDropConfirm();

    void closeSaveLoadMenu(SaveLoadMenuMgr& saveLoadMenu, const PauseMenuMgr& pauseMenu);
    void closePauseMenu(PauseMenuMgr& pauseMenu, SaveLoadMenuMgr& saveLoadMenu);

    void closeAll(
        InventoryMgr& inventoryMgr,
        TakeMgr& takeMgr,
        InteractionMgr& interactionMgr,
        SpeakTargetMgr& speakTargetMgr,
        DropConfirmMgr& dropConfirmMgr,
        PauseMenuMgr& pauseMenu,
        SaveLoadMenuMgr& saveLoadMenu);

    bool isSidePanelOpen(
        const InventoryMgr& inventoryMgr,
        const TakeMgr& takeMgr,
        const InteractionMgr& interactionMgr,
        const SpeakTargetMgr& speakTargetMgr) const;

    private:
    UiMode mode = UiMode::Gameplay;
};

}

#endif /* UI_COORDINATOR_H */