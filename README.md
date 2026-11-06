# Grace Valley

A cozy city builder about growing a welcoming valley community. Build roads and homes, open chapels, stock a community pantry, and make room for parks. Each passing week brings new neighbors, community support, upkeep, and choices about where to grow next.

**Status:** early playable prototype. The simulation and its tests are C++20; the graphical game uses SDL3. The supplied logo appears in the game and as its window icon; town art is currently drawn with simple shapes so gameplay can be tuned before producing assets.

## Play the prototype

| Action | Control |
| --- | --- |
| Select road, home, chapel, pantry, park | `1` through `5` |
| Select a building with the mouse | Click its numbered tool in the top bar |
| Inspect chapel or pantry coverage | Select tool `3` or `4` |
| Prioritize pantry or chapel volunteers | `V` |
| Host a community gathering | `E` (30 funds) |
| Organize a food drive | `F` (25 funds) |
| Place selected building | Left click |
| Draw roads | Select road (`1`), then hold left click and drag; fast drags fill skipped tiles |
| Remove a building | Right click |
| Pause or resume | `Space` |
| Advance one week while paused | `N` |
| Pan the camera | `WASD`, arrow keys, or middle mouse drag |
| Zoom at the pointer | Mouse wheel |
| Zoom from the center | `+` and `-` |
| Reset the camera | `R` |
| Quit | `Esc` |

Extend roads from the starting chapel. Homes and services need a connected road beside them. If removing a road cuts off a district, affected tiles get red outlines and connected homes lose capacity until the route is repaired. The chapel's starting road cannot be removed. The pond blocks construction; building in woods clears them for 10 extra funds. The top bar shows the current week, population, joy, funds, and active tool. The lower bar shows access and placement guidance. A placement preview turns red when a tile is invalid or unaffordable.

Roads now draw as connected streets: straight runs, corners, junctions, and dead ends change shape as you build. Hovering a valid road tile shows the proposed connection and updates its neighboring road shapes in the preview.
Start a road stroke on a placeable tile or any existing road. Dragging builds each reachable tile along the path and charges its usual construction cost; the stroke stops at blocked or unaffordable tiles. After deleting a link, you can build from an isolated road to reconnect it to the chapel. Isolated roads and their buildings remain inactive until the connection is restored.

With a chapel or pantry selected, **green** shows the reach of staffed buildings, **gold** shows new reach from a valid hovered placement, and **red markers** identify homes that still need the selected service or would lose it after the proposed placement. An **orange outline** on a connected service means it needs volunteers. The lower bar shows covered connected homes and the net change the proposed building would cause. The preview accounts for volunteer staffing; a placeable building may show no new coverage when it cannot be staffed. Service reach currently uses a five-tile Manhattan radius; road access is required, but travel along roads is not yet simulated.

Each home has **belonging** and **care** needs that change weekly. Staffed chapels help belonging, staffed pantries help care, and parks help both. Volunteers grow with population; when there are too few to staff every service, `V` chooses whether pantries or chapels get first priority. Gatherings boost belonging and arrivals, while food drives boost care. Events last two weeks, reserve two volunteers, and share a six-week cooldown. Hover over a home to inspect its needs.

### Funds

The top bar shows **IN**, **OUT**, and **NET** for the next week at the current town size. Weekly income is a small founding grant (10) plus `1.5 × population`, rounded down. Weekly upkeep pays for homes (1 each), chapels (4), pantries (3), parks (1), roads (1 per three road tiles, rounded up), and growing resident support and coordination costs (`population / 2 + population² / 150`, rounded down by each term). Funds fall when **OUT** exceeds **IN**. Construction and events are extra one-time expenses. Building is blocked if it cannot be paid for, but removing buildings is free and can bring upkeep down. The founding grant keeps a pared-back town from being permanently trapped in debt.

This is a first balance pass. Compact, spread, and event-led towns remain viable in the automated scenarios; excessive expansion without service planning creates a deficit. Human playtesting will decide whether the pace and costs feel fair.

## Build in Visual Studio

Install Visual Studio 2022 or newer with **Desktop development with C++** and CMake support. Open this repository folder in Visual Studio. It will read `CMakePresets.json`; select `windows-debug` or `windows-release`, then build and run `grace-valley`.

From a developer PowerShell:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

On this machine, CMake is bundled at `D:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` if `cmake` is not on `PATH`. The Debug executable is `build/windows-debug/Debug/grace-valley.exe`.

On macOS or Linux:

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug
ctest --preset desktop-debug --output-on-failure
```

The first configure downloads SDL3 3.4.18 unless a compatible SDL3 package is already installed. Set `GRACE_FETCH_SDL=OFF` to use an installed SDL3 package only. Set `GRACE_BUILD_GAME=OFF` for a dependency-free simulation and test build.

## Verification

`ctest` runs rule checks and accelerated compact, spread, and event-led town scenarios. For a headless render check on Windows, set `SDL_VIDEODRIVER=dummy` and run the executable with `--capture build/coverage-preview.png` or `--capture-roads build/roads-preview.png`. The `--soak 1200` argument runs an automated 20-minute session with periodic building and events, then prints its final town state. These modes are for development; a person still needs to judge pacing and clarity with the [playtest script](docs/playtest.md).

## Project layout

```text
assets/           Future licensed art, sound, and fonts
docs/             Design, architecture, and milestones
include/grace/     Simulation public headers
src/core/          Platform-independent simulation
src/game/          SDL3 window, input, and prototype rendering
tests/            Rule tests and accelerated town scenarios
```

## Direction

The design lives in [docs/game-design.md](docs/game-design.md), the technical plan in [docs/technical-plan.md](docs/technical-plan.md), and the sequence of work in [docs/milestones.md](docs/milestones.md).

The setting is fictional and welcoming across traditions. The game is about community care and city planning, with joy and hard choices; it does not score belief or make residents into resources.

## Development

CMake is the single source of truth for Visual Studio and other desktop toolchains. Keep simulation rules independent of SDL and add tests for rule changes. Third-party dependencies are not checked into the repository. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

The project's license is not chosen yet. Until one is added, the source and assets remain under their respective authors' rights. SDL3 is used under its own license; see [docs/technical-plan.md](docs/technical-plan.md).
