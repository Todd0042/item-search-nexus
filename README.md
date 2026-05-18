# Gw2ItemSearch

A Guild Wars 2 addon for [Nexus](https://raubana.xyz/nexus/) that searches for items across your entire account — bank, shared inventory, material storage, all characters (bags, equipment, and equipment tabs), trading post delivery and sell orders.

Originally created by [Pentose](https://github.com/tentose) as a Blish HUD module, ported to a native Nexus DLL.

## Features

- Search items by name with type/rarity/subtype/stat filters
- Browse your full inventory using filter-only mode (no search query needed)
- View item stats, upgrades, infusions, and containment info in tooltips
- Parallel downloading (12 connections) — full 73k item database in ~30s
- Parallel character scanning (6 workers) — scans all chars simultaneously
- Zero in-game lag during background loading
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
2. Copy it into your addons folder (typically `Guild Wars 2/Addons/` inside your Guild Wars 2 installation directory)
3. Launch the game — the setup wizard will appear. Enter your API key and click "Save and Start"
4. Toggle the search window with the default hotkey Alt+F or click the Nexus QuickAccess icon

## First-time load

The item database (~73k items) is downloaded using 12 parallel connections, then all characters are scanned in parallel with 6 workers. A full first load takes about **1 minute**. Zero in-game lag.

Subsequent loads are much faster — bank, materials, shared inventory, and trading post are always refreshed, but individual characters are only re-scanned if they've gained playtime since the last scan.

## Building from source

Requirements: CMake 3.20+, MinGW-w64 (GCC 13+), Ninja.

```
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -B build
ninja -C build
```

Output: `build/Gw2ItemSearch.dll`

## License

MIT License — see [LICENSE](LICENSE).

This project is a port of the [Gw2ItemSearch](https://github.com/tentose/Gw2ItemSearch) Blish HUD module by [Pentose](https://github.com/tentose) (tentose), used under the MIT License.

## Credits

- [Pentose](https://github.com/tentose) — original [Gw2ItemSearch](https://github.com/tentose/Gw2ItemSearch) Blish HUD module (MIT)
- [Blish HUD](https://github.com/blish-hud/Blish-HUD) — standalone overlay for Guild Wars 2
- [TaimiHUD](https://github.com/agaertner/TaimiHUD) — reference Nexus addon implementation
