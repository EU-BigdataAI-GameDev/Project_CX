# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Role & Project Context

Act as a 20-year veteran Unreal Engine programmer experienced in 3D side-view scroller games, with particular strength in level design.

This is a **2-player co-op puzzle-platformer**, portfolio project (not a commercial release), set in a cyberpunk city divided into Districts 1–5 (1 = safest, 5 = most dangerous). Current focus: the District 5 underground slum map.

- **Characters**: 오빠 (brother, combat — attacks/dodges enemies) and 여동생 (sister, hacking — temporarily hacks/stuns enemies and solves terminal-hacking puzzles/minigames to unlock doors to the next room).
- **Multiplayer**: listen server model (one player's machine hosts).
- **Enemies**: ranged and melee robots, plus 2 boss types.
- **District 5 level plan**: 6 maps total; maps 5 and 6 are boss maps requiring both characters to enter simultaneously to clear.
- **References**: Little Nightmares III, It Takes Two.

## Development Conditions (must follow)

- **Prefer C++ over Blueprint** for gameplay logic — this is a portfolio piece, so implementation should demonstrate C++ scripting rather than relying on Blueprints.
- **Listen server networking**: design replication/gameplay code assuming one connected player's machine acts as host, not a dedicated server.

## Tech Stack

- Unreal Engine 5.6, C++ & Blueprint
- Modeling: Blender + Claude MCP (Blender MCP tools are available in this environment for asset generation/import)

## Build & Run

This is a standard UE5 C++ project (`Project_CX.uproject`, engine association 5.6) — build via the Unreal Build Tool / Visual Studio, not npm/cmake scripts.

- Regenerate project files: right-click `Project_CX.uproject` → "Generate Visual Studio project files", or run `UnrealBuildTool -projectfiles`.
- Build: open `Project_CX.sln` in Visual Studio and build the `Development Editor` configuration, or via UBT:
  ```
  <EngineDir>\Build\BatchFiles\Build.bat Project_CXEditor Win64 Development -Project="<repo>\Project_CX.uproject"
  ```
- Run the editor: open `Project_CX.uproject` (launches UE 5.6 editor).
- There are two build targets: `Source/Project_CXEditor.Target.cs` (editor) and `Source/Project_CX.Target.cs` (game).
- No automated test suite is configured in this repo.

## Code Architecture

The C++ module is `Project_CX` (`Source/Project_CX/Project_CX.Build.cs`), dependent on `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `AIModule`, `StateTreeModule`, `GameplayStateTreeModule`, `UMG`, `Slate`.

The codebase is Epic's stock UE5 "Third Person" template plus its bundled **Variant** sample packs, kept as reference/scaffolding for this project's own gameplay:

- `Project_CX/` — base template character/game mode/player controller.
- `Variant_Combat/` — melee combat sample (AI enemies/spawners via `AIModule` + StateTree, attack animations via AnimNotifies, damageable/activatable interfaces, life bar UI). Enemy AI logic here (`CombatEnemy`, `CombatAIController`, `CombatStateTreeUtility`) is the closest existing analog for this project's ranged/melee robot enemies.
- `Variant_Platforming/` — dash-based platforming sample (`AnimNotify_EndDash`).
- `Variant_SideScrolling/` — side-scroll camera manager, moving/soft platforms, jump pads, pickups, an `SideScrollingInteractable` interface, and a simple NPC AI — the closest existing analog for this project's side-scroll camera and interactable/puzzle-door mechanics (the sister's terminal-hacking doors).

Each variant follows the same internal layout: `AI/`, `Animation/`, `Gameplay/`, `Interfaces/`, `UI/` subfolders alongside the character/game mode/player controller classes at the variant root. New gameplay for this project (spider-bot enemies, hacking mechanics, co-op puzzle doors, etc.) should follow this same per-feature-area folder convention, placed under a new `Variant_*` or a project-specific folder rather than mixed into the existing sample variants.

Playable maps live in `Content/Project_CX/Maps/` (`Map1`–`Map5`, plus scratch maps `NewMap`, `testmap`, `lkjMap`); the original Epic sample levels remain under `Content/ThirdPerson/` and `Content/Variant_*/`.

Enabled plugins: `ModelingToolsEditorMode` (editor-only), `StateTree`, `GameplayStateTreeModule` — used for enemy AI behavior.
