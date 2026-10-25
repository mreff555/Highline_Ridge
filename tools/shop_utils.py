#!/usr/bin/env python3
"""Boilerplate helpers for ridge merchant shops: catalogs, dialog, and post-purchase flow."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

ONES = {
    0: "zero",
    1: "one",
    2: "two",
    3: "three",
    4: "four",
    5: "five",
    6: "six",
    7: "seven",
    8: "eight",
    9: "nine",
    10: "ten",
    11: "eleven",
    12: "twelve",
    13: "thirteen",
    14: "fourteen",
    15: "fifteen",
    16: "sixteen",
    17: "seventeen",
    18: "eighteen",
    19: "nineteen",
}
TENS = {
    2: "twenty",
    3: "thirty",
    4: "forty",
    5: "fifty",
    6: "sixty",
    7: "seventy",
    8: "eighty",
    9: "ninety",
}


@dataclass
class ShopDialogConfig:
    """Standard merchant dialog; every field may be overridden per shop."""

    merchant_tone: str = "haberdasher"
    phase_id: str = "shopkeeper"
    catalog_choice_id: str = "what_on_offer"

    intro: str = ""
    resume_intro: str = ""

    catalog_offer_label: str = "Yes, what do you have on offer?"
    catalog_offer_response: str = ""

    just_browsing_label: str = "Just browsing at the moment."
    just_browsing_response: str = ""

    catalog_done_label: str = "Thanks, just browsing."
    catalog_done_response: str = ""

    buy_label: str = "I'll take it."
    decline_label: str = "Thanks, maybe next time."

    anything_else_question: str = ""
    look_again_label: str = "Let me take another look."
    look_again_response: str = ""
    no_thanks_label: str = "No, thanks."
    no_thanks_response: str = ""

    extra_top_level_choices: list[dict[str, Any]] = field(default_factory=list)

    # When True, catalog opens via openActorInventory and stock lives in
    # actorInventories instead of nested browse/buy choice trees (#60).
    # Default False so haberdashery custom-order trees keep working until cut over.
    use_actor_inventory: bool = False
    # Actor id / bag key for openActorInventory (defaults to phase_id).
    inventory_actor_id: str = ""


def format_price(amount: float) -> str:
    if abs(amount - round(amount)) < 0.001:
        return f"${int(round(amount))}"
    return f"${amount:.2f}"


def _under_hundred(n: int) -> str:
    if n < 20:
        return ONES[n]
    tens, ones = divmod(n, 10)
    if ones == 0:
        return TENS[tens]
    return f"{TENS[tens]}-{ONES[ones]}"


def _dollars_phrase(dollars: int) -> str:
    if dollars == 0:
        return ""
    if dollars == 1:
        return "one dollar"
    return f"{_under_hundred(dollars)} dollars"


def _cents_phrase(cents: int) -> str:
    if cents == 0:
        return ""
    if cents == 1:
        return "one cent"
    return f"{_under_hundred(cents)} cents"


def price_words(amount: float) -> str:
    cents_total = int(round(amount * 100))
    dollars, cents = divmod(cents_total, 100)
    dollar_text = _dollars_phrase(dollars)
    cent_text = _cents_phrase(cents)
    if dollar_text and cent_text:
        return f"{dollar_text} and {cent_text}"
    return dollar_text or cent_text or "nothing"


def grant_item(item: dict) -> dict:
    return {
        "id": item["id"],
        "name": item["name"],
        "icon": f"resources/icons/{item['icon']}",
        "examineImage": f"resources/images/{item['examine']}",
        "examineText": item["description"],
    }


def _tone_defaults(config: ShopDialogConfig) -> ShopDialogConfig:
    if config.merchant_tone == "hardware":
        if not config.intro:
            config.intro = (
                "The merchant straightens from a ledger behind the counter and meets your "
                "eyes without hurry. He is tall, broad through the chest and shoulders, "
                "built like a man who has lifted iron all his life and meant to keep doing "
                "it. His shirt is clean, his suspenders straight, his beard trimmed with "
                "the same discipline he brings to stock and shelf.\n\n"
                "\"Afternoon. What can I help you find?\""
            )
        if not config.resume_intro:
            config.resume_intro = (
                "He rests both hands on the counter, patient and square-shouldered. "
                "\"What else do you need?\""
            )
        if not config.catalog_offer_response:
            config.catalog_offer_response = (
                "\"Everything you see and most of what you don't.\" He jerks his "
                "chin toward the floor stock and wall tools. \"Picks, shovels, "
                "rakes, drills, hammers, anvils, nails, wire, rope, lanterns, "
                "mining goods. If a man means to build or dig at altitude, I stock "
                "for it.\""
            )
        if not config.just_browsing_response:
            config.just_browsing_response = (
                "\"Then browse,\" he says. \"Mind the barrels and the low lantern "
                "hooks. Holler if you want something down from the wall.\""
            )
        if not config.catalog_done_response:
            config.catalog_done_response = (
                "\"Suit yourself,\" he says, not unkindly. "
                "\"Walk the aisles. Holler if you need a hand.\""
            )
        if not config.anything_else_question:
            config.anything_else_question = "\"Need anything else while you're here?\""
        if not config.look_again_response:
            config.look_again_response = (
                "\"Take your time,\" he says, nodding toward the stock."
            )
        if not config.no_thanks_response:
            config.no_thanks_response = (
                "\"Fair enough,\" he says. \"Door's open when you need us.\""
            )
    else:
        if not config.intro:
            config.intro = (
                "The gentleman behind the counter sets down his pen and regards you with "
                "practiced courtesy. His vest is pressed, his collar crisp, his hands clean "
                "in the manner of a man who sells refinement at altitude.\n\n"
                "\"Good day, sir. Is there anything I can help you find?\""
            )
        if not config.resume_intro:
            config.resume_intro = (
                "\"Now then, sir,\" he says, settling his hands on the counter. "
                "\"What else may I help you find?\""
            )
        if not config.catalog_offer_response:
            config.catalog_offer_response = (
                "\"Well... anything you see here.\" He sweeps one hand across the "
                "shelves and counter with the easy pride of a man who knows his "
                "stock by heart. \"We also do custom jackets, shirts, vests, and "
                "trousers.\""
            )
        if not config.just_browsing_response:
            config.just_browsing_response = (
                "\"Of course, sir,\" he says warmly. \"Take your time. "
                "I will be here if you need anything.\""
            )
        if not config.catalog_done_response:
            config.catalog_done_response = (
                "\"Very well, sir,\" he says pleasantly. "
                "\"Take your time with the room. I am here if you change your mind.\""
            )
        if not config.anything_else_question:
            config.anything_else_question = "\"Anything else I may help you find?\""
        if not config.look_again_response:
            config.look_again_response = (
                "\"By all means, sir,\" he says, stepping back from the counter."
            )
        if not config.no_thanks_response:
            config.no_thanks_response = (
                "\"A pleasure, sir,\" he says with a small bow. \"Do call again.\""
            )
    return config


def _merchant_pitch(item: dict, config: ShopDialogConfig) -> str:
    price = item["price"]
    if config.merchant_tone == "hardware":
        return f"\"Solid choice,\" he says. \"That'll be {price_words(price)}.\""
    return f"\"Excellent choice, sir,\" he says. \"That will be {price_words(price)}.\""


def _merchant_purchase(item: dict, config: ShopDialogConfig) -> str:
    if config.merchant_tone == "hardware":
        return (
            f"He sets the {item['name'].lower()} on the counter with both hands, "
            "the way a man handles weight he respects. \"Good iron. It'll serve you.\""
        )
    return (
        f"He wraps the {item['name'].lower()} with the care of a man who believes "
        "presentation is part of the purchase. \"A pleasure doing business with you, sir.\""
    )


def _merchant_decline(config: ShopDialogConfig) -> str:
    if config.merchant_tone == "hardware":
        return "\"No trouble,\" he says. \"Stock's not going nowhere.\""
    return (
        "\"Of course, sir,\" he says with a courteous nod. "
        "\"The goods will be here when you return.\""
    )


def post_transaction_followups(config: ShopDialogConfig) -> list[dict[str, Any]]:
    return [
        {
            "id": "shop_look_again",
            "label": config.look_again_label,
            "response": config.look_again_response,
            "resumeChoiceId": config.catalog_choice_id,
            "closePhase": False,
        },
        {
            "id": "shop_no_thanks",
            "label": config.no_thanks_label,
            "response": config.no_thanks_response,
            "closePhase": True,
        },
    ]


def wrap_transaction_leaf(
    choice: dict[str, Any],
    config: ShopDialogConfig,
    *,
    is_purchase: bool,
) -> dict[str, Any]:
    wrapped = dict(choice)
    wrapped["closePhase"] = False
    response = wrapped.get("response", "").rstrip()
    if config.anything_else_question:
        response = f"{response}\n\n{config.anything_else_question}"
    wrapped["response"] = response
    wrapped["choices"] = post_transaction_followups(config)
    if not is_purchase:
        wrapped.pop("requiresMoney", None)
        wrapped.pop("grantItem", None)
        wrapped.pop("status", None)
    return wrapped


def apply_post_transaction_to_leaves(
    choices: list[dict[str, Any]],
    config: ShopDialogConfig,
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for choice in choices:
        entry = dict(choice)
        nested = entry.get("choices")
        if nested:
            entry["choices"] = apply_post_transaction_to_leaves(nested, config)
            result.append(entry)
            continue
        if entry.get("id") == "catalog_done":
            result.append(entry)
            continue
        choice_id = entry.get("id", "")
        if choice_id.startswith("buy_"):
            result.append(wrap_transaction_leaf(entry, config, is_purchase=True))
        elif choice_id.startswith("decline_"):
            result.append(wrap_transaction_leaf(entry, config, is_purchase=False))
        else:
            result.append(entry)
    return result


def standard_purchase_choice(
    item: dict,
    *,
    config: ShopDialogConfig,
) -> dict[str, Any]:
    price = item["price"]
    return {
        "id": f"browse_{item['id']}",
        "label": f"{item['name']} - {format_price(price)}",
        "response": _merchant_pitch(item, config),
        "choices": [
            {
                "id": f"buy_{item['id']}",
                "label": config.buy_label,
                "requiresMoney": price,
                "response": _merchant_purchase(item, config),
                "grantItem": grant_item(item),
                "status": {"money": -price},
            },
            {
                "id": f"decline_{item['id']}",
                "label": config.decline_label,
                "response": _merchant_decline(config),
            },
        ],
    }


def custom_purchase_choice(
    item: dict,
    *,
    config: ShopDialogConfig,
    sample_item: dict,
) -> dict[str, Any]:
    price = item["price"]
    sample_price = sample_item["price"]
    return {
        "id": f"browse_{item['id']}",
        "label": f"{item['name']} - {format_price(price)}",
        "response": (
            "\"Excellent, sir. If you have the funds today I can begin taking measurements, "
            "and it should be about six weeks' lead time.\" He straightens slightly, pride "
            "entering his voice like a pressed crease. \"All of my custom clothes are designed "
            "and constructed on Savile Row in London.\""
        ),
        "choices": [
            {
                "id": f"buy_{item['id']}",
                "label": "Let's do this",
                "requiresMoney": price,
                "response": (
                    "He produces his measuring tape without another word and begins the ritual "
                    "of numbers and posture, writing each measure as though recording scripture. "
                    "\"Very good, sir. Six weeks. I will send word when it arrives from London.\""
                ),
                "grantItem": grant_item(item),
                "status": {"money": -price},
            },
            {
                "id": f"fabric_{item['id']}",
                "label": (
                    "I don't have the funds today, just browsing. "
                    "I do really like that fabric though."
                ),
                "response": (
                    "He follows your gesture to the bolt rolls and nods with quiet pride.\n\n"
                    "\"Yes, this is my more premium fabric—one hundred percent long-grain wool. "
                    f"If you would like to consider it, I can get you a sample for {price_words(sample_price)}.\""
                ),
                "choices": [
                    {
                        "id": f"buy_fabric_from_{item['id']}",
                        "label": config.buy_label,
                        "requiresMoney": sample_price,
                        "response": (
                            "He cuts a small square from the bolt with scissors that barely "
                            "whisper, folds it into a card, and ties it with twine. "
                            "\"For consideration, sir.\""
                        ),
                        "grantItem": grant_item(sample_item),
                        "status": {"money": -sample_price},
                    },
                    {
                        "id": f"decline_fabric_from_{item['id']}",
                        "label": config.decline_label,
                        "response": (
                            "\"No trouble at all, sir,\" he says. "
                            "\"The cloth isn't going anywhere.\""
                        ),
                    },
                ],
            },
            {
                "id": f"decline_{item['id']}",
                "label": config.decline_label,
                "response": (
                    "\"Of course, sir,\" he says. "
                    "\"Custom work requires patience on both sides.\""
                ),
            },
        ],
    }


def catalog_choices(
    items: list[dict],
    *,
    config: ShopDialogConfig | None = None,
    merchant_tone: str = "haberdasher",
    sample_item: dict | None = None,
    browse_done_label: str | None = None,
    browse_done_response: str | None = None,
) -> list[dict[str, Any]]:
    """Legacy nested browse/buy tree. Prefer actor_inventory_bag + openActorInventory (#60)."""
    if config is None:
        config = ShopDialogConfig(merchant_tone=merchant_tone)
    config = _tone_defaults(config)
    if config.merchant_tone != merchant_tone:
        config.merchant_tone = merchant_tone
        config = _tone_defaults(config)

    if browse_done_label is not None:
        config.catalog_done_label = browse_done_label
    if browse_done_response is not None:
        config.catalog_done_response = browse_done_response

    choices: list[dict[str, Any]] = []
    for item in items:
        if sample_item is not None and item.get("custom"):
            choices.append(
                custom_purchase_choice(item, config=config, sample_item=sample_item)
            )
        elif item.get("custom"):
            raise ValueError(f"custom item {item['id']} requires sample_item")
        else:
            choices.append(standard_purchase_choice(item, config=config))

    choices.append(
        {
            "id": "catalog_done",
            "label": config.catalog_done_label,
            "response": config.catalog_done_response,
        }
    )
    return apply_post_transaction_to_leaves(choices, config)


def actor_inventory_bag(
    items: list[dict],
    *,
    quantity: int | None = 1,
) -> list[dict[str, Any]]:
    """Build SceneSpeakConfig.actorInventories[<actor>].items entries (#60)."""
    bag: list[dict[str, Any]] = []
    for item in items:
        entry: dict[str, Any] = {
            "id": item["id"],
            "price": float(item["price"]),
        }
        if quantity is None:
            entry["quantity"] = None  # infinite stock
        else:
            entry["quantity"] = int(quantity)
        bag.append(entry)
    return bag


def build_shop_main_phase(
    items: list[dict],
    *,
    config: ShopDialogConfig,
    sample_item: dict | None = None,
    requires_flag: str = "",
) -> dict[str, Any]:
    config = _tone_defaults(config)
    actor_id = config.inventory_actor_id or config.phase_id

    offer: dict[str, Any] = {
        "id": config.catalog_choice_id,
        "label": config.catalog_offer_label,
        "response": config.catalog_offer_response,
    }
    if config.use_actor_inventory:
        if any(item.get("custom") for item in items):
            raise ValueError(
                "use_actor_inventory does not support custom-order branches; "
                "set use_actor_inventory=False for haberdashery-style catalogs"
            )
        offer["openActorInventory"] = actor_id
    else:
        offer["choices"] = catalog_choices(
            items, config=config, sample_item=sample_item
        )

    phase: dict[str, Any] = {
        "id": config.phase_id,
        "type": "scripted",
        "resetOnSceneEnter": True,
        "intro": config.intro,
        "resumeIntro": config.resume_intro,
        "choices": [
            offer,
            *config.extra_top_level_choices,
            {
                "id": "just_browsing",
                "label": config.just_browsing_label,
                "response": config.just_browsing_response,
            },
        ],
    }
    if requires_flag:
        phase["requiresFlag"] = requires_flag
    return phase


def build_shop_conversation(
    items: list[dict],
    *,
    config: ShopDialogConfig,
    sample_item: dict | None = None,
    greeting_phases: list[dict[str, Any]] | None = None,
    requires_flag: str = "",
) -> dict[str, Any]:
    config = _tone_defaults(config)
    phases = list(greeting_phases or [])
    phases.append(
        build_shop_main_phase(
            items,
            config=config,
            sample_item=sample_item,
            requires_flag=requires_flag,
        )
    )
    out: dict[str, Any] = {"speakPhases": phases}
    if config.use_actor_inventory:
        actor_id = config.inventory_actor_id or config.phase_id
        out["actorInventories"] = {
            actor_id: {"items": actor_inventory_bag(items)},
        }
    return out