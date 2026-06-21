#!/usr/bin/env python3
"""Shared helpers for ridge shop item catalogs and 1891 pricing."""

from __future__ import annotations

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


def standard_purchase_choice(
    item: dict,
    *,
    merchant_tone: str = "haberdasher",
    sample_item: dict | None = None,
) -> dict:
    price = item["price"]
    label = f"{item['name']} - {format_price(price)}"

    if merchant_tone == "hardware":
        pitch = (
            f"\"Solid choice,\" he says. \"That'll be {price_words(price)}.\""
        )
        purchase = (
            f"He sets the {item['name'].lower()} on the counter with both hands, "
            "the way a man handles weight he respects. \"Good iron. It'll serve you.\""
        )
        decline = (
            "\"No trouble,\" he says. \"Stock's not going anywhere.\""
        )
    else:
        pitch = (
            f"\"Excellent choice, sir,\" he says. \"That will be {price_words(price)}.\""
        )
        purchase = (
            f"He wraps the {item['name'].lower()} with the care of a man who believes "
            "presentation is part of the purchase. \"A pleasure doing business with you, sir.\""
        )
        decline = (
            "\"Of course, sir,\" he says with a courteous nod. "
            "\"The goods will be here when you return.\""
        )

    return {
        "id": f"browse_{item['id']}",
        "label": label,
        "response": pitch,
        "choices": [
            {
                "id": f"buy_{item['id']}",
                "label": "I'll take it.",
                "requiresMoney": price,
                "response": purchase,
                "grantItem": grant_item(item),
                "status": {"money": -price},
            },
            {
                "id": f"decline_{item['id']}",
                "label": "Thanks, maybe next time.",
                "response": decline,
            },
        ],
    }


def custom_purchase_choice(item: dict, *, sample_item: dict) -> dict:
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
                        "label": "I'll take it.",
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
                        "label": "Thanks, maybe next time.",
                        "response": (
                            "\"No trouble at all, sir,\" he says. "
                            "\"The cloth isn't going anywhere.\""
                        ),
                    },
                ],
            },
            {
                "id": f"decline_{item['id']}",
                "label": "Thanks, maybe next time.",
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
    merchant_tone: str = "haberdasher",
    sample_item: dict | None = None,
    browse_done_label: str = "Thanks, just browsing.",
    browse_done_response: str = "",
) -> list[dict]:
    choices = []
    for item in items:
        if sample_item is not None and item.get("custom"):
            choices.append(custom_purchase_choice(item, sample_item=sample_item))
        elif item.get("custom"):
            raise ValueError(f"custom item {item['id']} requires sample_item")
        else:
            choices.append(
                standard_purchase_choice(item, merchant_tone=merchant_tone)
            )

    if not browse_done_response:
        if merchant_tone == "hardware":
            browse_done_response = (
                "\"Suit yourself,\" he says, not unkindly. "
                "\"Walk the aisles. Holler if you need a hand.\""
            )
        else:
            browse_done_response = (
                "\"Very well, sir,\" he says pleasantly. "
                "\"Take your time with the room. I am here if you change your mind.\""
            )

    choices.append(
        {
            "id": "catalog_done",
            "label": browse_done_label,
            "response": browse_done_response,
        }
    )
    return choices