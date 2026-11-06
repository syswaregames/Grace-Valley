# Technical plan

## Stack decision

**C++20 + CMake + SDL3** is the initial stack. CMake opens directly in Visual Studio and also supports macOS/Linux toolchains. SDL3 handles the window, input, audio later, and a 2D renderer with platform-specific drivers. Its official CMake documentation covers Visual Studio and Unix builds, and its renderer can select available Direct3D, Metal, Vulkan, OpenGL, or software drivers by platform. We pin SDL3 3.4.18 for reproducible prototype builds.

The code separates `grace_core` from `grace-valley`. Simulation state, rules, and tests have no SDL dependency. This keeps weekly updates deterministic and permits a future graphics/UI change without rewriting the town rules.

## Graphics roadmap

| Stage | Choice | Reason |
| --- | --- | --- |
| Prototype | SDL3 2D renderer | Minimal integration; enough for tiles, overlays, sprites, and clear interaction |
| Visual slice | SDL3 renderer with atlased sprites and camera | Establish isometric readability and performance budget |
| If advanced 3D/shaders become essential | Evaluate SDL GPU or bgfx with a measured prototype | SDL GPU offers modern graphics control; bgfx abstracts Direct3D, Metal, Vulkan, and OpenGL. Both add shader and pipeline complexity |

There is no need to manually load a different graphics DLL for each platform. CMake links SDL3, and SDL chooses a supported renderer at runtime. The Windows build copies SDL3.dll beside the executable. Distribution on each platform will need its own packaging and license review.

References: [SDL3 CMake guide](https://wiki.libsdl.org/SDL3/README-cmake), [SDL3 renderer drivers](https://wiki.libsdl.org/SDL3/SDL_HINT_RENDER_DRIVER), [SDL3 platforms](https://github.com/libsdl-org/SDL/blob/main/docs/README-platforms.md), [SDL3 releases](https://github.com/libsdl-org/SDL/releases), [bgfx overview](https://github.com/bkaradzic/bgfx/blob/master/docs/overview.rst).

## Architecture to grow into

```text
Input -> commands -> World update (fixed weekly tick) -> snapshot -> renderer/UI
                       |                         |
                  rules + data              save/load later
```

- Use a fixed simulation step independent of frame rate.
- Make building costs, capacities, and service radii data driven after prototype tuning.
- Extend the current road graph into path distances before introducing traffic, delivery, or service travel time.
- Seed random events and record the seed in save files for reproducibility.
- Version save data and add migration tests before public builds.
- Keep rendering one way: the UI reads snapshots and issues commands; it does not mutate rules directly.

## Quality gates

Each rule milestone gets core tests for boundaries and consequences. Build Debug and Release on Windows and at least one macOS and Linux runner once packaging begins. Measure frame time on a growing map before choosing a more complex renderer. Profile the simulation separately from drawing.

## Dependency and rights notes

SDL3 uses the zlib license; preserve its notice with redistributed binaries. Do not add art, fonts, or audio without a known redistribution license. Choose a project source license before accepting outside contributions or shipping a release.
