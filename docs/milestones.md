# Milestones

Each milestone ends with something the player can run or evaluate. Status was last reviewed on **2026-11-06**. Dates for future work are intentionally unset until the team and time budget are known.

| Milestone | Status | Playable result | Exit criteria |
| --- | --- | --- | --- |
| M0 — Foundation | **Complete** | Buildable C++ project and design direction | CMake opens in Visual Studio; core tests pass; SDL window opens |
| M1 — First valley | **Playtest pending** | Place roads, homes, chapel, pantry, park; watch weekly growth | Valid placement and costs; understandable on-screen feedback; no crashes during 20 minutes of play |
| M2 — Town anatomy | **Complete for prototype** | Connected road network, camera, zoom, land terrain | Disconnected districts behave correctly; map can be navigated; overlays explain access |
| M3 — Community life | **Complete for prototype** | Household needs, care coverage, volunteer pool, events | At least two viable town strategies; service shortages produce understandable effects |
| M4 — Identity | Planned | Isometric art pass, seasons, music, resident stories | A new player understands buildings and town state without developer guidance |
| M5 — Resilience | **In progress** | Versioned single-slot save/load; larger maps, performance, accessibility still planned | Save compatibility tests; stable frame time at target town size; remappable controls and readable UI |
| M6 — Public demo | Planned | Tutorial, balance, packaging on Windows/macOS/Linux | Complete 45–60 minute arc; platform smoke tests; asset and license audit |

## Progress evidence

- **M0:** Visual Studio Debug and Release builds, core tests, and SDL startup smoke tests pass. The simulation and scenario tests also pass with GCC in Ubuntu/WSL, including AddressSanitizer and UndefinedBehaviorSanitizer. The user confirmed the game runs on this machine. Full graphical macOS/Linux builds remain a later platform gate.
- **M1:** Eight road, zone, civic, and power tools, weekly growth, costs, and placement feedback are playable. A compact two-column right toolbar keeps every build tool visible; top buttons control Play, Budget, and an expandable Options menu for policies, events, camera, and stepping one week while paused. New games begin in Setup with weekly cashflow paused. A connected power plant is required for zone growth, and amber badges identify outages. The budget panel shows a forecast and lets players set giving and service spending. Bankruptcy has a screen showing the final finances, with mouse controls to inspect the budget or restart. A costly budget reaches bankruptcy, while compact, spread, and event-led towns remain solvent through the 24-week scenarios. Human usability and pacing feedback still remain before closing M1.
- **M2:** The starting chapel anchors the road network; road cuts disconnect buildings and repair restores them. Pan, zoom, access warnings, and service coverage overlays work. The pond blocks construction; woods cost extra to clear. Residential, commercial, and industrial zones appear as vacant lots and develop only from demand, road access, and available power. A connected plant supplies 12 units through the road network; outage badges distinguish power shortages. Commuters travel from homes to jobs on connected roads; animated cars, amber/red congestion, and traffic alerts expose bottlenecks. Local roads, avenues, boulevards, and arterials have increasing capacity, cost, and upkeep. Captured frames were inspected for visual readability.
- **M3:** Each home tracks care and belonging. Staffing consumes volunteers, priority can switch between chapels and pantries, and orange outlines identify unstaffed services. Gatherings and food drives use funds and volunteers. Demand bars show the pull for homes, shops, and workshops; business tax trades current revenue for future growth. A visible City Score combines joy, parks, services, access, power, traffic, and cashflow; it affects residential, commercial, and industrial demand. A congestion relief test confirms that all three demands rise when a busy corridor is upgraded. Compact, spread, and event-led automated strategies remain viable after 24 weeks with a power plant; a costly budget reaches bankruptcy in week 5. These values remain prototype tuning evidence, not a substitute for player feedback.
- **M5:** Manual Save/Load buttons and F5/F9 shortcuts use a versioned file in the platform user data folder. A world round trip preserves town rules and continues with the same next-week result; the UI smoke test saves, changes, and reloads a town. The new road-level format loads previous version 1 saves; unknown and truncated versions are rejected. Multiple slots, autosave, broader future migration, and the remaining M5 exit criteria are still planned.

## Immediate next increments

1. Complete the [20-minute playtest](playtest.md) and record confusing actions, pacing, and crashes; then close M1 if the exit criteria hold.
2. Playtest zoning, demand bars, tax policy, and bankruptcy. Check whether the growth tradeoffs are clear and whether budget warnings give players enough time to respond.
3. Replace the temporary debug font HUD with a polished, scalable UI and tooltips.
4. Move toward M4 with seasonal presentation, resident stories, and an isometric art experiment.

## Risks and decisions to revisit

- The prototype's economy is intentionally simple; test whether it creates meaningful choices before adding more resources.
- Isometric art can hide road and service logic; overlays and silhouettes need early validation.
- The church theme needs thoughtful writing and varied residents. Review stories with people familiar with different communities.
- Decide whether the final game favors a relaxed sandbox, scenario challenges, or both after M3 playtests.
