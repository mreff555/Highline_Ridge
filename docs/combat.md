# Fight resolution (Timberline scrape)

Conversations arm an **Attack** button with `allowAttack` + `attackEncounterId` on a speak line. Clicking Attack plays authored **lead-up** text/TTS, runs a weighted **scrape**, then plays the **win** or **lose** bag. Outcome uses optional **actor edge** and a light random swing.

Implementation: `src/CombatResolver.h`, `GameSession::resolveCombatEncounter`. Encounter defs live in `conversations.json` under `sceneId.attackEncounters[id]` (`AttackEncounterDef` in `ConversationStruct.h`).

Scene editor: **FightAuthoringDialog** (Edit Fight…) authors lead/win/lose bags (parchment + Generate TTS / Generate voice), actor edge, loss %, reputation, and availability gates — dual-writes `attackEncounters` and sets `allowAttack` / `attackEncounterId` on the parent line. See [conversations.md](conversations.md).

The saloon `drunk_patron` encounter is the first authored example.

---

## Runtime flow

1. Line with `allowAttack: true` + `attackEncounterId` plays (with or without companion choices).
2. Conversation awaits; **Attack** enables when the encounter’s availability gates pass.
3. Player clicks **Attack** → lead-up text/TTS → scrape → win or lose text/TTS.
4. On lose: optional per-stat % of *current*, optional player death; optional reputation delta (signed, not `%`).

Debug: `TIMBERLINE_DEBUG_COMBAT=1` prints scores / roll / outcome on stderr.

---

## Weights (sum = 1.0)

| Stat | Weight | Role |
|------|--------|------|
| **Resolve** | **0.35** | Grit — heaviest |
| Health | 0.20 | Take a hit |
| Energy | 0.20 | Keep swinging |
| **Charisma** | **0.15** | Presence / intimidation / crowd |
| Lucidity | 0.10 | Clarity under pressure |

---

## Scores

Stats are **0–100** (`PlayerStats`).

```
playerScore = Σ W_s × P_s
actorScore  = Σ W_s × clamp(50 + A_s, 0, 100)
margin      = playerScore − actorScore
```

`A_s` = authored actor modifier for that stat (default **0** = even mid-stat opponent).

- **Positive `A_s`** → actor **advantage** (harder fight)
- **Negative `A_s`** → actor **disadvantage** (easier)

Example: actor Resolve **+25** raises `actorScore` by `0.35 × 25 ≈ 8.75`.

---

## Roll

```
roll  = Uniform(0, 20)
check = margin + roll
```

| Outcome | Condition |
|---------|-----------|
| **Win** | `cannotLose` **or** `check ≥ 10` |
| **Lose** | otherwise |

Threshold **10** is the midpoint of the roll: an even matchup (`margin ≈ 0`) is roughly a coin flip; high Resolve or a player lead shifts the curve without guaranteeing victory.

---

## JSON shape (`attackEncounters`)

```json
"attackEncounters": {
  "drunk_patron": {
    "id": "drunk_patron",
    "combatantId": "drunk_patron",
    "actorEdge": { "health": 10, "energy": -5, "resolve": -10, "lucidity": -15, "charisma": 5 },
    "leadUp": { "text": "...", "tts": true, "ttsVoice": "leo", "ttsText": "...", "ttsAudio": "..." },
    "win": { "text": "...", "tts": true, "ttsVoice": "leo", "ttsText": "..." },
    "lose": { "text": "...", "tts": true, "ttsVoice": "leo", "ttsText": "..." },
    "cannotLose": false,
    "playerDeath": false,
    "loss": {
      "health": { "enabled": true, "percentOfCurrent": 33 },
      "energy": { "enabled": true, "percentOfCurrent": 40 },
      "charisma": { "enabled": true, "percentOfCurrent": 10 }
    },
    "reputation": { "enabled": true, "delta": -3 },
    "availability": {}
  }
}
```

Availability mirrors choice gates: money / insufficient money, actor tab pairs, saloon room, light source, inventory item, story flag, plus `DialogRequirementFields` (day/lucidity/opinion).

---

## Worked numbers

Default-ish player (H90 E20 R50 L30 C50), all actor mods 0:

- `playerScore ≈ 50`, `actorScore = 50`, `margin = 0` → need `roll ≥ 10` to win  

Resolve **80** instead of 50: `margin ≈ +10.5` → usually win.

`drunk_patron` default edge (soaked brawler): Health +10, Energy −5, Resolve −10, Lucidity −15, Charisma +5.
