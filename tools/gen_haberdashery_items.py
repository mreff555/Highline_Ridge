#!/usr/bin/env python3
"""Generate haberdashery item entries and conversation catalog helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from shop_utils import catalog_choices  # noqa: E402

ITEMS = [
    {
        "id": "mens_cologne",
        "name": "Men's Cologne",
        "price": 4.50,
        "weightLb": 0.42,
        "icon": "mens_cologne_icon.png",
        "examine": "mens_cologne_examine.png",
        "description": (
            "A heavy glass bottle of men's cologne with a ground stopper and a label "
            "printed in restrained copperplate. The scent is cedar, bergamot, and something "
            "drier beneath—tobacco leaf, perhaps, or the memory of it. The Ridge Haberdashery "
            "wraps its goods as though altitude might bruise them."
        ),
        "custom": False,
    },
    {
        "id": "straight_razor",
        "name": "Straight Razor",
        "price": 12.00,
        "weightLb": 0.22,
        "icon": "straight_razor_icon.png",
        "examine": "straight_razor_examine.png",
        "description": (
            "A straight razor in a fitted leather sheath, the blade hollow-ground and "
            "polished to a mirror that shows your face in fragments. The handle is horn, "
            "warm from the maker's bench, pinned with a brass tang. The edge is sharp enough "
            "to part a hair without complaint."
        ),
        "custom": False,
    },
    {
        "id": "hair_tonic",
        "name": "Hair Tonic",
        "price": 0.75,
        "weightLb": 0.28,
        "icon": "hair_tonic_icon.png",
        "examine": "hair_tonic_examine.png",
        "description": (
            "A cobalt bottle of hair tonic with a cork sealed in red wax. The label promises "
            "restorative virtue in language more medical than honest. The liquid smells of "
            "bay rum and barber soap. A man's vanity in a bottle small enough to hide in a "
            "coat pocket."
        ),
        "custom": False,
    },
    {
        "id": "shaving_brush",
        "name": "Shaving Brush",
        "price": 2.50,
        "weightLb": 0.14,
        "icon": "shaving_brush_icon.png",
        "examine": "shaving_brush_examine.png",
        "description": (
            "A shaving brush with a turned handle of pale boxwood and a knot of badger hair "
            "dense enough to hold a proper lather. The ferrule is nickel silver, stamped "
            "with a London maker's mark worn thin at the edge. Used with care, it would last "
            "longer than most resolutions on the High Line."
        ),
        "custom": False,
    },
    {
        "id": "cuff_links",
        "name": "Cuff Links",
        "price": 14.00,
        "weightLb": 0.08,
        "icon": "cuff_links_icon.png",
        "examine": "cuff_links_examine.png",
        "description": (
            "A pair of cuff links in a small velvet tray: onyx faces set in sterling, "
            "engine-turned at the border so they catch lamplight without shouting. The posts "
            "click shut with the satisfying precision of good mechanism. The sort of detail "
            "a man buys when he wants a room to revise its estimate of him."
        ),
        "custom": False,
    },
    {
        "id": "bow_tie",
        "name": "Bow Tie",
        "price": 2.75,
        "weightLb": 0.05,
        "icon": "bow_tie_icon.png",
        "examine": "bow_tie_examine.png",
        "description": (
            "A bow tie of black silk grosgrain, pre-tied on a band but finished well enough "
            "that the deception would pass at a distance. The fabric has a dry hand and a "
            "subtle rib that speaks of formal rooms and better lighting than the ridge "
            "usually affords."
        ),
        "custom": False,
    },
    {
        "id": "custom_shirt",
        "name": "Custom Men's Shirt",
        "price": 18.00,
        "weightLb": 0.55,
        "icon": "custom_shirt_icon.png",
        "examine": "custom_shirt_examine.png",
        "description": (
            "A measurement card and fabric swatch pinned to a tailor's ticket for a custom "
            "shirt: long-grain cotton, mother-of-pearl buttons, collar cut to your measure. "
            "The haberdasher notes a six-week lead time and, with quiet pride, Savile Row "
            "construction. The garment itself is not here yet. The promise is."
        ),
        "custom": True,
    },
    {
        "id": "custom_trousers",
        "name": "Custom Men's Trousers",
        "price": 22.00,
        "weightLb": 0.72,
        "icon": "custom_trousers_icon.png",
        "examine": "custom_trousers_examine.png",
        "description": (
            "A tailor's order for custom trousers: waist, inseam, and rise recorded in ink "
            "that does not smudge. The cloth sample is worsted wool, dark enough for church "
            "and boardrooms that do not exist on the High Line but might in Graysmill. "
            "Six weeks, the card says. Made on Savile Row in London."
        ),
        "custom": True,
    },
    {
        "id": "custom_jacket",
        "name": "Custom Men's Jacket",
        "price": 45.00,
        "weightLb": 1.35,
        "icon": "custom_jacket_icon.png",
        "examine": "custom_jacket_examine.png",
        "description": (
            "A commission card for a custom jacket: shoulder width, chest, and lapel style "
            "noted with the care of a man who believes fit is morality. The fabric sample is "
            "long-grain wool in charcoal, substantial enough to hold a ridge wind at bay. "
            "Six weeks' lead. Designed and constructed on Savile Row in London, as though "
            "the address itself were a kind of guarantee."
        ),
        "custom": True,
    },
    {
        "id": "mens_pea_coat",
        "name": "Men's Pea Coat",
        "price": 32.00,
        "weightLb": 2.8,
        "icon": "mens_pea_coat_icon.png",
        "examine": "mens_pea_coat_examine.png",
        "description": (
            "A men's pea coat of heavy navy melton wool, double-breasted with anchor buttons "
            "and a collar that can be turned against wind that has never learned manners. The "
            "lining is quilted. The shoulders are broad. It smells of new cloth and cold air, "
            "as though it has been waiting for a ship that docks at altitude."
        ),
        "custom": False,
    },
    {
        "id": "mens_duster",
        "name": "Men's Duster",
        "price": 24.00,
        "weightLb": 3.1,
        "icon": "mens_duster_icon.png",
        "examine": "mens_duster_examine.png",
        "description": (
            "A full-length men's duster in oiled canvas the color of winter road dust, with "
            "a split back for riding and pockets deep enough to lose a man's intentions. The "
            "metal buttons are darkened against tarnish. It is less garment than declaration: "
            "a man who expects distance, weather, and the kind of work that keeps his hands "
            "dirty."
        ),
        "custom": False,
    },
    {
        "id": "wool_fabric_sample",
        "name": "Wool Fabric Sample",
        "price": 0.25,
        "weightLb": 0.02,
        "icon": "wool_fabric_sample_icon.png",
        "examine": "wool_fabric_sample_examine.png",
        "description": (
            "A small cut of premium long-grain wool, one hundred percent, folded into a card "
            "and tied with twine. The hand is dense and cool, the weave tight enough that "
            "light passes reluctantly. The haberdasher's note on the card reads simply: "
            "for consideration."
        ),
        "custom": False,
    },
]

SAMPLE_ITEM = next(item for item in ITEMS if item["id"] == "wool_fabric_sample")
CATALOG_ITEMS = [item for item in ITEMS if item["id"] != "wool_fabric_sample"]


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

    catalog = catalog_choices(
        CATALOG_ITEMS,
        merchant_tone="haberdasher",
        sample_item=SAMPLE_ITEM,
    )
    out = ROOT / "tools" / "haberdashery_catalog.json"
    out.write_text(json.dumps(catalog, indent=2) + "\n")
    print(f"Wrote {len(ITEMS)} items and {len(catalog)} catalog choices")


if __name__ == "__main__":
    main()