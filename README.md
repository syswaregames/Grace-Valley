# Grace Valley

A cozy city builder about growing a welcoming valley community. Build roads, power, and neighborhoods; open chapels, stock a community pantry, and make room for parks. Each passing week brings new neighbors, community support, upkeep, and choices about where to grow next.

**Status:** early playable prototype. The simulation and its tests are C++20; the graphical game uses SDL3. The supplied logo appears in the game and as its window icon; town art is currently drawn with simple shapes so gameplay can be tuned before producing assets.

## Play the prototype

| Action | Control |
| --- | --- |
| Select road, residential, commercial, industrial, chapel, pantry, park, power plant | `1` through `8` |
| Select a tool with the mouse | Click an icon in the two-column right toolbar; small numbers show keyboard shortcuts |
| Clear the build tool | Click **Pan 0**, press `0` or `H`, or press `Esc`; left-click no longer places tiles |
| Demolish roads, zones, or buildings | Select **Bulldozer** or press `9`, then click or drag with the left mouse button |
| Open the Options menu | Click **Options** at the top; it contains play, budget, policy, event, camera, and single-week actions |
| Open town reports | Click **Reports** or press `I`; the map stays clear until you ask for demand, service, traffic, and wellbeing details |
| Review the annual report | It opens at the start of each year; reopen the latest report from **Options > Annual Report** |
| Inspect power supply | Open Reports, then click **Power** or press `Tab` to compare capacity, current consumption, projected need, and surplus after all zones develop |
| Choose a power source | Click **Power Sources** in the sidebar, then select fuel, solar, or wind and place it beside a connected road |
| View city overlays | Click **Map Views** to display power, traffic, pollution, noise, or service coverage; select **None** to clear it |
| Review city issues | Open Reports and select **Issues** to see active alerts and suggested fixes |
| Follow traffic advice | Click the top **Traffic** alert to open the Issues tab; it identifies the busiest road and suggests a route or maintenance fix |
| Read zone demand at a glance | The top HUD keeps compact Residential (`R`), Commercial (`C`), and Industrial (`I`) bars visible; hover for a management tip |
| Review financial trends | Open Reports to see a 12-week funds chart and income-versus-expenses chart |
| Save the current town | Click **Save** at the top or press `F5` |
| Load the saved town | Click **Load** at the top or press `F9` |
| Inspect chapel or pantry coverage | Select tool `5` or `6` |
| Prioritize pantry or chapel volunteers | `V` |
| Host a community gathering | `E` (30 funds) |
| Organize a food drive | `F` (25 funds) |
| Place selected building | Left click |
| Remove a road, zone, or building | Select the bulldozer (`9`), then left click or drag; the right mouse button does not demolish |
| Move the map | Hold the right mouse button and drag on the map with any tool selected, or use `WASD` / arrow keys |
| Draw roads | Select road (`1`), then hold left click and drag; fast drags fill skipped tiles |
| Build a bridge | With Road selected, click or drag from an existing road across water |
| Upgrade a busy road | Click **Upgrade Road** in the sidebar or press `U`, then click road tiles; `Shift` + left click is a shortcut |
| Paint zoning areas | Select residential (`2`), commercial (`3`), or industrial (`4`); hold left click and drag |
| Start the weekly simulation / pause or resume | Click the play/pause symbol or press `Space` |
| Open the budget | Click **Budget** or press `B` (click a policy to select it) |
| Get financial help | When projected cash runway falls to about three weeks, the city pauses and the advisor recommends budget changes |
| Borrow emergency funds | Click **Borrow 200** in Budget or the advisor; one loan may be active at a time |
| Cycle giving policy | `G` |
| Cycle service budget | `O` |
| Cycle business tax | `T` (low, balanced, high) |
| Cycle road maintenance | `M` (low, balanced, generous) |
| Start a new town after bankruptcy | `Backspace` |
| Remove a building | Right click |
| Advance one week while paused, after starting | `N` |
| Pan the camera across the expanded 48 × 32 valley | `WASD`, arrow keys, or middle mouse drag |
| Zoom at the pointer; survey the whole valley | Mouse wheel |
| Zoom from the center | `+` and `-` |
| Reset the camera | `R` |
| Inspect a map item | Hover without moving for about 1.2 seconds |
| Close a menu / quit | `Esc` closes an open menu; press it again to quit |

New games begin in **Setup** with the weekly clock stopped. Lay out roads and zones, build a power plant, choose civic facilities, and set the budget before clicking the play symbol or pressing `Space`. Zoning and construction still cost funds during setup. Extend roads from the starting chapel. Buildings need a connected road beside them. If removing a road cuts off a district, affected tiles get red outlines and connected homes lose capacity until the route is repaired. The chapel's starting road cannot be removed. Buildings cannot occupy the pond, but roads can cross it as bridges; building in woods clears them for 10 extra funds. The top bar keeps week, finances, City Score, and the primary controls visible. The right toolbar keeps the build tools visible. Click **Reports** or press `I` to see demand, community health, roads, services, and budget summaries when you need them. Placement guidance stays in the slim footer; invalid or unaffordable placements show a red preview.

### Save and load

Click **Save** or press `F5` to write the current town. Click **Load** or press `F9` to restore it, including the week, finances, buildings, roads and upgrades, household needs, policies, events, camera, selected tool, and Play/Pause state. There is one save slot; saving again replaces it. A missing or incompatible save shows a message and leaves the current town intact.

On Windows the file is `%APPDATA%\SyswareGames\GraceValley\grace-valley.sav`. SDL chooses the equivalent user data folder on macOS and Linux. The file is versioned; towns saved with the earlier two-level road format still load. Save/Load is currently manual, with no autosave or multiple slots.

Roads now draw as connected streets: straight runs, corners, junctions, and dead ends change shape as you build. Hovering a valid road tile shows the proposed connection and updates its neighboring road shapes in the preview.
Start a road stroke on a placeable tile or any existing road. Dragging builds each reachable tile along the path and charges its usual construction cost; the stroke stops at blocked or unaffordable tiles. After deleting a link, you can build from an isolated road to reconnect it to the chapel. Isolated roads and their buildings remain inactive until the connection is restored.

When a road reaches water, the Road tool builds a **bridge** automatically. Each water tile costs **25 funds** and adds **1 fund of weekly upkeep**, on top of ordinary road network upkeep. Bridges carry vehicles and distribute power like land roads. Hovering a water tile shows the bridge cost; click or drag from an existing road to cross. Removing a bridge tile restores water and can cut off the far bank. Bridges can receive the same road upgrades as other roads.

### Cars and traffic

Residents commute from powered homes to powered shops and workshops along connected roads. Small cars show the resulting flow. Each ordinary road tile handles 6 trips; overloaded tiles turn **amber**, then **red** at more than twice their capacity. The top bar reports traffic alerts, the sidebar shows the number of hotspots and peak flow, and hovering a road shows its traffic and capacity. Each congested tile lowers town happiness by 2, up to a 12-point penalty.

The top bar also shows a **City Score** from 0 to 100. Joy, connected parks, and homes served by both a chapel and pantry improve it; congestion, power outages, lost road access, damaged roads, and a weekly deficit lower it. Each congested road removes another 4 score points, up to 28, in addition to its effect on joy. The score changes all three zoning demands: residential responds most, commercial next, and industrial less. A score above 60 helps growth; a score below 60 holds it back. If it stays below 30 for three weeks, a home or business is abandoned every three weeks. Abandoned buildings stop providing housing capacity, jobs, and tax; their boarded-up shells remain until removed or the City Score stays above 45 for three weeks, when one reopens. Hover over the meter for a reminder of its factors. Fixing a busy corridor can raise the score and demand without placing new zones.

Click **Upgrade Road** in the sidebar or press `U`, then click a road tile to advance it one level. `Shift` + left click upgrades directly. Each level has a distinct surface and badge:

| Road level | Trip capacity | Cost to build or upgrade | Added weekly upkeep per tile |
| --- | ---: | ---: | ---: |
| Local road | 6 | 5 to build | 0 |
| Avenue | 14 | 20 from Local | 1 |
| Boulevard | 24 | 35 from Avenue | 3 |
| Arterial | 36 | 55 from Boulevard | 6 |

The usual road network upkeep also applies. A busy corridor may need several upgraded tiles. Right clicking removes a road and all its upgrades. Commuters currently choose a shortest road route, so parallel streets may need later route-choice tuning.

### Zoning and demand

Mark residential, commercial, and industrial plots during Setup or while the simulation runs. Zoning has an upfront cost, but it leaves a visibly vacant R/C/I lot. At the start of each week, the game uses the demand shown in the bars: a lot can develop into a home, shop, or workshop only when it touches the connected road network, has spare power, and its demand is at least 25. At most one lot of each kind develops per week. If demand, road access, or power is missing, the lot stays empty. Use the Bulldozer (`9`) to clear a vacant zone or its finished building.

Build a **power plant** beside a connected road with tool `8`. Each plant costs 40 funds, supplies 12 kW, and costs 6 funds per week in upkeep. Homes use 1 kW, shops 2 kW, and workshops 3 kW. Solar costs 100 funds, supplies 8 kW, and costs 2 per week; wind costs 75, supplies 6 kW, and costs 3 per week. Both are clean and provide steady output; weather is not yet simulated. These figures are simplified for gameplay. Connected roads distribute power; no separate wire tool is needed yet. Existing buildings receive power before vacant zones. The Power map view shows whether each vacant zone could individually use the current spare power; it does not reserve that power for every zone. Reports > Power compares plant capacity with current building consumption and projected need if all empty zones develop. **Amber lightning badges** mark zones that cannot develop and occupied buildings that have lost power. Unpowered homes provide no capacity, while unpowered shops and workshops provide no jobs or taxes. Removing the plant or its road link cuts supply immediately. The fuel power plant emits smoke while connected: homes within four tiles suffer noise and pollution, reducing care, belonging, City Score, and residential demand. Workshop and plant effects can stack.

The compact **R**, **C**, and **I** bars in the top HUD show zone demand from 0 to 100. Powered shops provide four jobs and powered workshops provide six, increasing residential demand. Connected parks and household happiness also attract residents. More residents raise business demand. Workshops create a noisy pollution radius: homes within four tiles receive a purple **N** marker, lose care and belonging, and reduce City Score and residential demand. The top bar shows a noise alert with the number of affected homes; keep housing and industry farther apart to protect livability. Developed businesses pay tax and incur upkeep; disconnecting them or losing power stops their jobs and tax revenue while their upkeep remains.

With a chapel or pantry selected, **green** shows the reach of staffed buildings, **gold** shows new reach from a valid hovered placement, and **red markers** identify homes that still need the selected service or would lose it after the proposed placement. An **orange outline** on a connected service means it needs volunteers. The lower bar shows covered connected homes and the net change the proposed building would cause. The preview accounts for volunteer staffing; a placeable building may show no new coverage when it cannot be staffed. Service reach currently uses a five-tile Manhattan radius; road access is required, but travel along roads is not yet simulated.

Each home has **belonging** and **care** needs that change weekly. Staffed chapels help belonging, staffed pantries help care, and parks help both. Volunteers grow with population; when there are too few to staff every service, `V` chooses whether pantries or chapels get first priority. Gatherings boost belonging and arrivals, while food drives boost care. Events last two weeks, reserve two volunteers, and share a six-week cooldown. Hover over a home to inspect its needs.

### Funds

The top bar shows **IN**, **OUT**, and **NET** for the next week at the current town size. Press `B` for the budget breakdown and clickable policies. The founding grant is 10 per week. **Giving** can be Gentle (1 per resident), Balanced (1.5 per resident, rounded down), or Ambitious (2 per resident). Gentle giving slightly helps household wellbeing; Ambitious giving strains it. **Services** can be Lean (half service upkeep, rounded up), Balanced, or Generous (double service upkeep). Funding changes how strongly staffed chapels and pantries meet needs. **Road Maintenance** can be Lean, Balanced, or Generous. Lean halves road upkeep but roads lose 12 condition each week, reducing capacity and increasing congestion; Generous doubles upkeep and repairs 8 condition per week. Damaged roads show an exclamation mark, lower City Score, and trigger an alert. **Business tax** can be Low, Balanced, or High. Low tax encourages new shops and workshops but collects less from each; High tax collects more from existing businesses but suppresses new business demand. Homes, shops, and workshops have fixed upkeep; resident support and coordination costs grow with population. Construction and events are separate one-time expenses. If weekly cashflow takes the treasury below zero, the town is **bankrupt** and the simulation ends. The bankruptcy screen shows the final balance and weekly cashflow. Click **View Budget** to inspect expenses or **New Town** to restart; `B` and `Backspace` are shortcuts.

This is an early balance pass. Compact, spread, and event-led towns remain viable in the automated scenarios. A costly service budget can cause bankruptcy; an expanding town can also run short of power. Human playtesting will decide whether the choices and pace feel fair.

The community loan provides 200 funds immediately. Every fourth week, the town pays 20 of principal plus 5% interest on the remaining balance. That payment appears in the week's expenses. Budget shows the outstanding balance and expected monthly payment; a new loan is available after repayment.

The calendar starts on January 1 of Year 1. Each game year has 52 weeks, arranged in twelve months using a repeating four, four, five-week pattern. At the new year, the game pauses for an annual report with recorded weekly income and expenses, opening and closing funds, population, and City Score. Construction, events, and loans affect the funds change separately from weekly cashflow. Older saves begin annual tracking when loaded, so their first report can cover a partial year.

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

`ctest` runs rule checks, accelerated town scenarios, and setup, road/bridge drag, zoning, power, traffic toolbar, and bankruptcy screen smoke tests. For a headless render check on Windows, set `SDL_VIDEODRIVER=dummy` and run the executable with `--capture build/coverage-preview.png`, `--capture-roads build/roads-preview.png`, `--capture-bridge build/bridge-preview.png`, `--capture-zones build/zoning-preview.png`, `--capture-traffic build/traffic-preview.png`, `--capture-budget build/budget-preview.png`, `--capture-options build/options-preview.png`, or `--capture-bankrupt build/bankruptcy-preview.png`. The `--soak 1200` argument runs an automated 20-minute session with periodic building and events, then prints its final town state. These modes are for development; a person still needs to judge pacing and clarity with the [playtest script](docs/playtest.md).

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
