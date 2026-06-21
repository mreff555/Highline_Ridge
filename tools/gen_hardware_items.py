#!/usr/bin/env python3
"""Generate alpine hardware item entries and conversation catalog."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from shop_utils import catalog_choices  # noqa: E402

ITEMS = [
    {
        "id": "mining_pick",
        "name": "Mining Pick",
        "price": 4.50,
        "weightLb": 4.8,
        "icon": "mining_pick_icon.png",
        "examine": "mining_pick_examine.png",
        "description": (
            "A prospector's pick with a hickory handle darkened by pitch and honest sweat. "
            "The head is forged steel, double-bit, with an edge that has been dressed recently "
            "on a wheel. Weighted for mountain ground: quartz, shale, and the stubborn ribs of "
            "ore that men chase above the timberline."
        ),
    },
    {
        "id": "shovel",
        "name": "Shovel",
        "price": 1.75,
        "weightLb": 3.6,
        "icon": "shovel_icon.png",
        "examine": "shovel_examine.png",
        "description": (
            "A round-point shovel with a long ash handle and a steel blade rolled at the edge. "
            "The socket is riveted true, the grip worn smooth where working hands have found "
            "the balance. Built for snow, gravel, and the endless digging that altitude demands."
        ),
    },
    {
        "id": "garden_rake",
        "name": "Garden Rake",
        "price": 1.25,
        "weightLb": 2.1,
        "icon": "garden_rake_icon.png",
        "examine": "garden_rake_examine.png",
        "description": (
            "A bow rake with sixteen forged tines and a handle cut from straight-grained ash. "
            "The head is pinned, not merely nailed, the way tools were made when men expected "
            "to keep them through more than one hard season."
        ),
    },
    {
        "id": "hand_drill",
        "name": "Hand Drill",
        "price": 2.50,
        "weightLb": 1.4,
        "icon": "hand_drill_icon.png",
        "examine": "hand_drill_examine.png",
        "description": (
            "A carpenter's brace-and-bit hand drill in beech and steel, the chuck tight, the "
            "crank worn bright at the palm. Three interchangeable bits ride in a cloth wrap: "
            "steel made for timber, cabinet work, and the small precise holes hardware men "
            "trust more than luck."
        ),
    },
    {
        "id": "blacksmith_hammer",
        "name": "Blacksmith's Hammer",
        "price": 2.00,
        "weightLb": 2.8,
        "icon": "blacksmith_hammer_icon.png",
        "examine": "blacksmith_hammer_examine.png",
        "description": (
            "A cross-peen blacksmith's hammer with a hickory handle shaped to the swing. The "
            "head carries the honest scars of work, peened and polished where countless blows "
            "have landed true. Not a wall decoration. A tool meant to move hot iron."
        ),
    },
    {
        "id": "mid_anvil",
        "name": "Mid-Sized Anvil",
        "price": 28.00,
        "weightLb": 68.0,
        "icon": "mid_anvil_icon.png",
        "examine": "mid_anvil_examine.png",
        "description": (
            "A mid-sized cast-steel anvil, roughly eighty pounds, with a clean face, hard horn, "
            "and a hardy hole dark from use. The waist is stamped with a Pittsburgh maker's "
            "mark. It rings when you tap it, a bright note that says the steel was tempered by "
            "someone who understood the trade."
        ),
    },
    {
        "id": "box_of_nails",
        "name": "Box of Nails",
        "price": 0.45,
        "weightLb": 1.0,
        "icon": "box_of_nails_icon.png",
        "examine": "box_of_nails_examine.png",
        "description": (
            "A pine box of cut common nails, four-penny weight, the heads slightly proud, the "
            "points sharp enough to bite on first strike. The lid is chalk-marked with the "
            "count and the date received. The small arithmetic of every cabin, fence, and "
            "mine brace on the ridge."
        ),
    },
    {
        "id": "copper_wire_spool",
        "name": "Spool of Copper Wire",
        "price": 1.60,
        "weightLb": 0.9,
        "icon": "copper_wire_spool_icon.png",
        "examine": "copper_wire_spool_examine.png",
        "description": (
            "A spool of bare copper wire, twelve-gauge, wound tight on a turned wooden core. "
            "The metal has the warm rose color of new copper and the faint oily sheen of stock "
            "kept dry against mountain damp. Useful for repairs, grounding lanterns, and the "
            "small improvisations men make when the valley is too far below."
        ),
    },
]


def main() -> None:
    items_path = ROOT / "resources" / "items.json"
    data = json.loads(items_path.read_text())
    for item in ITEMS:
        data["items"][item["id"]] = {
            "name": item["name"],
            "description": item["description"],
            "weightLb": item["weightLb"],
            "visuals": {"image": f"resources/images/{item['examine']}"},
            "icons": {"icon": f"resources/icons/{item['icon']}"},
        }
    items_path.write_text(json.dumps(data, indent=2) + "\n")

    catalog = catalog_choices(ITEMS, merchant_tone="hardware")
    out = ROOT / "tools" / "hardware_catalog.json"
    out.write_text(json.dumps(catalog, indent=2) + "\n")
    print(f"Wrote {len(ITEMS)} hardware items and {len(catalog)} catalog choices")


if __name__ == "__main__":
    main()