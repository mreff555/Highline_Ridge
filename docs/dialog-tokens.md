# In-game dialog tokens (text substitution)

Narrative and spoken **response text** can include curly-brace tokens that the
runtime replaces before display.

## Format

- Use **snake_case** inside braces: `{token_name}`
- Do **not** use spaces: `{tab amount}` is **not** supported
- Tokens are matched literally (case-sensitive)

Implemented in `GameSession` via `substituteDialogTokens()`.

## Built-in tokens

| Token | Meaning | Source |
|-------|---------|--------|
| `{tab_amount}` | Player’s open tab with the bartender, formatted as currency (e.g. `$1.50`) | `WorldState::actorTabOwedTo("bartender")` via `formatCurrency` |
| `{tab}` | Alias for `{tab_amount}` | Same |

Example (saloon bartender pay-tab flow in `resources/conversations.json`):

```text
That'll be {tab_amount}.
```

At runtime this becomes something like:

```text
That'll be $2.00.
```

## Where substitution runs

- **On-screen narrative / speak responses** — yes (`substituteDialogTokens`)
- **Bundled TTS audio** — **no** automatic substitution; clips are pre-rendered  
  Prefer writing TTS lines without dynamic money amounts, or regenerate TTS after
  changing static copy. Dynamic currency in voice would need a future runtime-TTS
  or multi-clip path.

## Adding a new token

1. Extend `substituteDialogTokens()` in `src/GameSession.cpp` with the new
   `{name}` and the value to insert.
2. Document it in this table.
3. Use only the documented `{snake_case}` form in `conversations.json` /
   scene text.

## Related

- Voice markup for TTS (`{{voice:eve}}…`) is separate — see TTS sections in
  `README.md` and `src/TtsVoiceMarkup.h`. That is **not** the same as
  `{tab_amount}`-style world tokens.
