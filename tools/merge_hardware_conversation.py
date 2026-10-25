#!/usr/bin/env python3
"""Merge alpine hardware shopkeeper conversation into conversations.json.

Uses actorInventories + openActorInventory (#60) instead of nested browse trees.
Preserves existing TTS fields on alpine_hardware when present.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from gen_hardware_items import ITEMS  # noqa: E402
from shop_utils import ShopDialogConfig, build_shop_conversation  # noqa: E402

COMPLIMENT_CHOICE = {
    "id": "compliment_building",
    "label": "Impressive building (looking around a bit)",
    "response": (
        "You take in the post-and-beam bones of the room: timbers joined "
        "with a joiner's patience, braces cut true, the whole frame standing "
        "with the quiet confidence of work done by someone who did not need "
        "to hurry.\n\n\"Impressive building,\" you say.\n\n"
        "For the first time, something like pleasure enters his face.\n\n"
        "\"I built it myself,\" he says. \"Every beam, every brace, every "
        "peg. My father ran a mill in Pennsylvania. I learned timber before "
        "I learned trade. Came up here in eighty-one when the High Line "
        "decided it was going to stay, not just camp. There was no proper "
        "hardware north of Graysmill that would make the climb worth a "
        "mule's temper, so I opened this room and stocked it for men who "
        "break ground in weather that kills carelessness.\" He taps the "
        "nearest post with his knuckle. \"She'll stand longer than most of "
        "the claims on this ridge.\""
    ),
    "consumeOnSelect": True,
    "persistConsumed": True,
    "resumeTopLevel": True,
    "closePhase": False,
}


def _merge_tts_fields(dst: dict, src: dict) -> None:
    """Copy known TTS keys from src onto dst when present."""
    for key, value in src.items():
        if key.startswith("tts") or key in {
            "resumeTts",
            "resumeTtsAudio",
            "resumeTtsText",
            "resumeTtsTextSha256",
            "resumeTtsVoice",
        }:
            dst[key] = value


def _merge_choice_tts(dst_choices: list[dict], src_choices: list[dict]) -> None:
    by_id = {c.get("id"): c for c in src_choices if isinstance(c, dict)}
    for choice in dst_choices:
        prev = by_id.get(choice.get("id"))
        if prev:
            _merge_tts_fields(choice, prev)


def main() -> None:
    conversations_path = ROOT / "resources" / "conversations.json"
    data = json.loads(conversations_path.read_text())
    previous = data.get("alpine_hardware", {})

    config = ShopDialogConfig(
        merchant_tone="hardware",
        phase_id="merchant",
        inventory_actor_id="merchant",
        use_actor_inventory=True,
        extra_top_level_choices=[COMPLIMENT_CHOICE],
    )

    fresh = build_shop_conversation(ITEMS, config=config)

    # Preserve authored intro / TTS / markup on the merchant phase and choices.
    prev_phases = previous.get("speakPhases") or []
    if prev_phases and fresh.get("speakPhases"):
        prev_phase = prev_phases[0]
        fresh_phase = fresh["speakPhases"][0]
        for key in (
            "intro",
            "resumeIntro",
            "ttsText",
            "ttsAudioSegments",
            "ttsVoice",
            "ttsAudio",
            "tts",
        ):
            if key in prev_phase:
                fresh_phase[key] = prev_phase[key]
        _merge_tts_fields(fresh_phase, prev_phase)
        # Keep the richer authored offer / compliment / browsing copy + TTS.
        if isinstance(prev_phase.get("choices"), list):
            prev_by_id = {
                c.get("id"): c for c in prev_phase["choices"] if isinstance(c, dict)
            }
            for choice in fresh_phase.get("choices", []):
                prev_choice = prev_by_id.get(choice.get("id"))
                if not prev_choice:
                    continue
                for key in ("label", "response"):
                    if key in prev_choice:
                        choice[key] = prev_choice[key]
                _merge_tts_fields(choice, prev_choice)
                # Never reintroduce nested catalog under the offer line.
                choice.pop("choices", None)
                if choice.get("id") == config.catalog_choice_id:
                    choice["openActorInventory"] = "merchant"

    data["alpine_hardware"] = fresh
    conversations_path.write_text(json.dumps(data, indent=2) + "\n")
    bag = fresh["actorInventories"]["merchant"]["items"]
    print(
        f"Updated alpine_hardware: actorInventories.merchant ({len(bag)} items), "
        "openActorInventory on what_on_offer"
    )


if __name__ == "__main__":
    main()
