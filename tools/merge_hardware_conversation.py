#!/usr/bin/env python3
"""Merge alpine hardware shopkeeper conversation into conversations.json."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    catalog = json.loads((ROOT / "tools" / "hardware_catalog.json").read_text())
    conversations_path = ROOT / "resources" / "conversations.json"
    data = json.loads(conversations_path.read_text())

    data["alpine_hardware"] = {
        "speakPhases": [
            {
                "id": "merchant",
                "type": "scripted",
                "resetOnSceneEnter": True,
                "intro": (
                    "The merchant straightens from a ledger behind the counter and meets your "
                    "eyes without hurry. He is tall, broad through the chest and shoulders, "
                    "built like a man who has lifted iron all his life and meant to keep doing "
                    "it. His shirt is clean, his suspenders straight, his beard trimmed with "
                    "the same discipline he brings to stock and shelf.\n\n"
                    "\"Afternoon. What can I help you find?\""
                ),
                "resumeIntro": (
                    "He rests both hands on the counter, patient and square-shouldered. "
                    "\"What else do you need?\""
                ),
                "choices": [
                    {
                        "id": "what_on_offer",
                        "label": "Yes, what do you have on offer?",
                        "response": (
                            "\"Everything you see and most of what you don't.\" He jerks his "
                            "chin toward the floor stock and wall tools. \"Picks, shovels, "
                            "rakes, drills, hammers, anvils, nails, wire, rope, lanterns, "
                            "mining goods. If a man means to build or dig at altitude, I stock "
                            "for it.\""
                        ),
                        "choices": catalog,
                    },
                    {
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
                    },
                    {
                        "id": "just_browsing",
                        "label": "Just browsing at the moment.",
                        "response": (
                            "\"Then browse,\" he says. \"Mind the barrels and the low lantern "
                            "hooks. Holler if you want something down from the wall.\""
                        ),
                    },
                ],
            }
        ]
    }

    conversations_path.write_text(json.dumps(data, indent=2) + "\n")
    print("Updated alpine_hardware conversation")


if __name__ == "__main__":
    main()