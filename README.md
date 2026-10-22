# Dead Space 3 — "No Hope" Difficulty Overhaul

A super-hard difficulty overhaul for **Dead Space 3 (2013 original, Visceral engine)**,
inspired by the Left 4 Dead 2 "No Hope" mod.

Because DS3 doesn't expose a scripting layer like L4D2 does, the practical route is
**Cheat Engine (runtime memory editing)** rather than editing game data files.

---

## Goal / Ruleset (edit to taste)

The "No Hope" idea = strip the safety nets and turn up the pressure. Starting targets:

- **Enemy damage** ×2 to ×3
- **Enemy health** ×1.5 to ×2
- **Player health** unchanged (or reduced for extra pain)
- **Resource / ammo drops** reduced or disabled
- **Med packs** rarer or weaker
- Optional: more aggressive / more frequent enemy spawns (hardest to reach without file modding)

Tune these numbers in `NoHope_DS3.lua` → the `Config` table.

---

## How this works (Cheat Engine)

Cheat Engine is a memory scanner. You run DS3, find the addresses that hold the values
above, and override them. CE has a full Lua engine, so instead of a static list of frozen
addresses we build a **script with toggles** that re-finds addresses via AOB
(array-of-bytes) scans each launch — this survives most game restarts.

### Workflow
1. Launch Dead Space 3.
2. Open Cheat Engine, attach it to the DS3 process.
3. Find a value (e.g. player health): scan → take damage → scan again → narrow it down.
4. Right-click the address → **"Find out what writes to this address"** to locate the
   instruction, then copy the surrounding bytes to build a stable **AOB signature**.
5. Record what you find in `addresses.md`.
6. Plug the signatures/offsets into `NoHope_DS3.lua`.
7. Load the Lua script in CE (Table → Show Cheat Table Lua Script, paste, Execute) or
   save the whole thing as a `.CT` table.

> ⚠️ Addresses change between game patches. AOB scans are more durable than raw addresses,
> but expect to re-verify after any DS3 update.

---

## How the DS1/DS2 "No Hope" tables are actually built (reference study)

The uploaded DS1 (v4.2) and DS2 (v1.5) tables were studied. Key architecture we
will mirror for DS3:

- **Framework:** both use TheyCallMeTim13's CE Lua framework (`I2CETableCEA`,
  logger levels, Save/Load Table State, Compact mode). Hooks live in embedded
  `.CEA` (auto-assembler) files loaded via `autoAssembleFile(...)`.
- **Difficulty = "Dec Hooks", not enemy scaling.** Instead of scaling enemy
  damage, they hook the instruction that *decrements* a player stat and multiply
  the delta:
  - `HealthDecHook` × >1  → you take more damage (the core "No Hope" pressure).
  - `StasisDecHook`, `OxygenDecHook` → faster drain / lower effective caps.
  - Infinite Ammo / Money / No Reload = the same dec hooks multiplied by 0.
  - `Health Inc Hook` scales healing (weaker med packs).
- **One struct, many stats.** A single hook captures the player struct base
  pointer; Health / Health Max / Stasis / Stasis Max are read as offsets from it
  (DS1: +124/+11C/+128/+12C; DS2: +EC/+E4/+F0/+F4). DS3 gets its own offsets.

This is why **Target #1 = find the player struct base** — it's the foundation the
whole table is built on. See `addresses.md`.

## Files
- `NoHope_DS3.lua` — the Cheat Engine script scaffold (config + toggle framework).
- `addresses.md` — worksheet for recording the struct base / offsets / AOB signatures.
- `README.md` — this file.

## What I (Claude) can help with
- Iterate the Lua logic and difficulty math.
- Turn your found AOB signatures into working toggles.
- Analyze DS3 data files if you extract them.

## What I can't do
- Scan your running game's memory for you — you have to find the addresses/offsets in CE
  and paste them here. Then I wire them up.

---

## Legal / scope note
For personal use with a game you own. This modifies the game in memory at runtime; it does
not redistribute any game files.
"# DS2008-Randomizer" 
