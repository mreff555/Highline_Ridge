/*******************************************************************************
 * Timberline engine — TTS resource validator (build-time hard fail)
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "TtsContentValidator.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::string scenes = "resources/scenes.json";
    std::string conversations = "resources/conversations.json";
    std::string items = "resources/items.json";
    // Craft TTS lives on product items; combinations path is optional/empty.
    std::string combinations;

    if (argc >= 4)
    {
        scenes = argv[1];
        conversations = argv[2];
        items = argv[3];
    }
    if (argc >= 5)
        combinations = argv[4];

    if (!timberline_engine::validateTtsResourcesOrLog(
            scenes, conversations, items, combinations))
    {
        std::cerr << "TTS validation failed.\n";
        return 1;
    }

    std::cout << "TTS validation OK.\n";
    return 0;
}
