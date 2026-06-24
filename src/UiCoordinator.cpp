#include "UiCoordinator.h"

#include <DropConfirmMgr.h>
#include <InteractionMgr.h>
#include <InventoryMgr.h>
#include <PauseMenuMgr.h>
#include <SaveLoadMenuMgr.h>
#include <SpeakTargetMgr.h>
#include <TakeMgr.h>

namespace testgame
{

void UiCoordinator::open(
    UiMode newMode,
    InventoryMgr& inventoryMgr,
    TakeMgr& takeMgr,
    InteractionMgr& interactionMgr,
    SpeakTargetMgr& speakTargetMgr,
    DropConfirmMgr& dropConfirmMgr,
    PauseMenuMgr& pauseMenu,
    SaveLoadMenuMgr& saveLoadMenu)
{
    if (newMode != UiMode::DropConfirm)
        closeAll(inventoryMgr, takeMgr, interactionMgr, speakTargetMgr, dropConfirmMgr, pauseMenu, saveLoadMenu);

    switch (newMode)
    {
        case UiMode::Inventory:
            inventoryMgr.open();
            break;
        case UiMode::Take:
            takeMgr.open();
            break;
        case UiMode::Interact:
            interactionMgr.open();
            break;
        case UiMode::Speak:
            speakTargetMgr.open();
            break;
        case UiMode::DropConfirm:
            break;
        case UiMode::Pause:
            pauseMenu.openMenu();
            break;
        case UiMode::SaveLoad:
            saveLoadMenu.openSaveMenu();
            break;
        case UiMode::Gameplay:
        default:
            break;
    }

    mode = newMode;
}

void UiCoordinator::closeAll(
    InventoryMgr& inventoryMgr,
    TakeMgr& takeMgr,
    InteractionMgr& interactionMgr,
    SpeakTargetMgr& speakTargetMgr,
    DropConfirmMgr& dropConfirmMgr,
    PauseMenuMgr& pauseMenu,
    SaveLoadMenuMgr& saveLoadMenu)
{
    inventoryMgr.close();
    takeMgr.close();
    interactionMgr.close();
    speakTargetMgr.close();
    dropConfirmMgr.close();
    pauseMenu.closeMenu();
    saveLoadMenu.closeMenu();
    mode = UiMode::Gameplay;
}

void UiCoordinator::openSaveMenu(SaveLoadMenuMgr& saveLoadMenu)
{
    mode = UiMode::SaveLoad;
    saveLoadMenu.openSaveMenu();
}

void UiCoordinator::openLoadMenu(SaveLoadMenuMgr& saveLoadMenu)
{
    mode = UiMode::SaveLoad;
    saveLoadMenu.openLoadMenu();
}

void UiCoordinator::openDropConfirm()
{
    mode = UiMode::DropConfirm;
}

void UiCoordinator::closeSaveLoadMenu(SaveLoadMenuMgr& saveLoadMenu, const PauseMenuMgr& pauseMenu)
{
    saveLoadMenu.closeMenu();
    mode = pauseMenu.isOpen() ? UiMode::Pause : UiMode::Gameplay;
}

void UiCoordinator::closePauseMenu(PauseMenuMgr& pauseMenu, SaveLoadMenuMgr& saveLoadMenu)
{
    saveLoadMenu.closeMenu();
    pauseMenu.closeMenu();
    mode = UiMode::Gameplay;
}

bool UiCoordinator::isSidePanelOpen(
    const InventoryMgr& inventoryMgr,
    const TakeMgr& takeMgr,
    const InteractionMgr& interactionMgr,
    const SpeakTargetMgr& speakTargetMgr) const
{
    return inventoryMgr.isOpen() || takeMgr.isOpen() || interactionMgr.isOpen() || speakTargetMgr.isOpen();
}

}