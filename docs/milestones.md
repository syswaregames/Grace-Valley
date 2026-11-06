# Milestones

Each milestone ends with something the player can run or evaluate. Status was last reviewed on **2026-11-06**. Dates for future work are intentionally unset until the team and time budget are known.

| Milestone | Status | Playable result | Exit criteria |
| --- | --- | --- | --- |
| M0 — Foundation | **Complete** | Buildable C++ project and design direction | CMake opens in Visual Studio; core tests pass; SDL window opens |
| M1 — First valley | **Playtest pending** | Place roads, homes, chapel, pantry, park; watch weekly growth | Valid placement and costs; understandable on-screen feedback; no crashes during 20 minutes of play |
| M2 — Town anatomy | **Complete for prototype** | Connected road network, camera, zoom, land terrain | Disconnected districts behave correctly; map can be navigated; overlays explain access |
| M3 — Community life | **Complete for prototype** | Household needs, care coverage, volunteer pool, events | At least two viable town strategies; service shortages produce understandable effects |
| M4 — Identity | Planned | Isometric art pass, seasons, music, resident stories | A new player understands buildings and town state without developer guidance |
| M5 — Resilience | Planned | Save/load, larger maps, performance, accessibility | Save compatibility tests; stable frame time at target town size; remappable controls and readable UI |
| M6 — Public demo | Planned | Tutorial, balance, packaging on Windows/macOS/Linux | Complete 45–60 minute arc; platform smoke tests; asset and license audit |

## Progress evidence

- **M0:** Visual Studio Debug and Release builds, core tests, and SDL startup smoke tests pass. The simulation and scenario tests also pass with GCC in Ubuntu/WSL, including AddressSanitizer and UndefinedBehaviorSanitizer. The user confirmed the game runs on this machine. Full graphical macOS/Linux builds remain a later platform gate.
- **M1:** All five building tools, weekly growth, costs, and placement feedback are playable. Automated compact, spread, and event-led scenarios stay stable for 24 weeks; compact and spread scenarios also run 480 simulated weeks. A 20-minute headless SDL autoplay finished without a crash or error output at week 478 (80 residents, joy 86, 79 events). Funds reached 55,802, which exposes a long-run economy balance issue. Human usability and pacing feedback still remain before closing M1.
- **M2:** The starting chapel anchors the road network; road cuts disconnect buildings and repair restores them. Pan, zoom, access warnings, and service coverage overlays work. The pond blocks construction; woods cost extra to clear. A captured frame was inspected for visual readability.
- **M3:** Each home tracks care and belonging. Staffing consumes volunteers, priority can switch between chapels and pantries, and orange outlines identify unstaffed services. Gatherings and food drives use funds and volunteers. Three automated strategies remain viable after 24 weeks: compact (joy 100, funds 367), spread (joy 100, funds 269), and event-led (joy 84, funds 228). These figures are prototype tuning evidence, not a substitute for player feedback.

## Immediate next increments

1. Complete the [20-minute playtest](playtest.md) and record confusing actions, pacing, and crashes; then close M1 if the exit criteria hold.
2. Tune the economy. The automated 20-minute town accumulated 55,802 funds despite building and events; longer scenarios need more decisions and costs.
3. Replace the temporary debug font HUD with a polished, scalable UI and tooltips.
4. Move toward M4 with seasonal presentation, resident stories, and an isometric art experiment.

## Risks and decisions to revisit

- The prototype's economy is intentionally simple; test whether it creates meaningful choices before adding more resources.
- Isometric art can hide road and service logic; overlays and silhouettes need early validation.
- The church theme needs thoughtful writing and varied residents. Review stories with people familiar with different communities.
- Decide whether the final game favors a relaxed sandbox, scenario challenges, or both after M3 playtests.
