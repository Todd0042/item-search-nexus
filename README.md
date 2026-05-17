# Gw2ItemSearch

A Guild Wars 2 addon for [Nexus](https://raubana.xyz/nexus/) that searches for items across your entire account — bank, shared inventory, material storage, all characters (bags, equipment, and equipment tabs), trading post delivery and sell orders.

Originally created by [Pentose](https://github.com/tentose) as a Blish HUD module, ported to a native Nexus DLL.

## Features

- Search items by name with type/rarity/subtype/stat filters
- Browse your full inventory using filter-only mode (no search query needed)
- View item stats, upgrades, infusions, and containment info in tooltips
- Background polling refreshes character gear as you play (no manual reload needed)
- Offline mode — shows cached data immediately while latest data is fetched
- Stat choice filter finds items across all rarities (e.g. "Berserker's" matches exotic and ascended)
- Icons loaded in background thread to avoid frame stutter
- Persists API key, cache, and settings across sessions

## Requirements

- Nedus addon loader
- A GW2 Api Key with at least: `account`, `inventories`, `characters`, `tradingpost`, `unlocks`

## Installation

1. Download `Gw2ItemSearch.dll` from [Releases](https://github.com/Todd0042/item-search-nexus/releases)
2. Copy it into your Nexus addons folder (typically `%appdata%/Nexus Data/Addons/`)
3. Launch the game, open Nexus (default: Alt+Z) → Options → Item Search → enter your API key and click "Save and Reload"
4. Toggle the search window with the default hotkey Alt+F or click the Nexus QuickAccess icon

## First-time load

The first scan fetches every item from every character's inventory, bank, material storage, and trading post. This can take several minutes (up to 10 for larger accounts). Subsequent loads are much faster — bank, materials, shared inventory, and trading post are always refreshed, but individual characters are only re-scanned if they've gained playtime since the last scan.

## Building from source

Requirements: CMake 3.20+, MinGW-w64 (GCC 13+), Ninja.

```
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -B build
ninja -C build
```

Output: `build/Gw2ItemSearch.dll`

## Credits

- [Pentose](https://github.com/tentose) — original Blish HUD module
- [TaimiHUD](https://github.com/agaertner/TaimiHUD) — reference Nexus addon implementation
