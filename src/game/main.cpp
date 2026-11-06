#include "grace/world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace {

constexpr float base_cell = 40.0f;
constexpr int margin = 20;
constexpr int header = 100;
constexpr int map_width = grace::World::width * 40;
constexpr int map_height = grace::World::height * 40;
constexpr int window_width = map_width + 2 * margin;
constexpr int window_height = map_height + header + 76;

struct Camera {
    float zoom = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    float cell_size() const { return base_cell * zoom; }
    void clamp() {
        const float spare_x = map_width - grace::World::width * cell_size();
        const float spare_y = map_height - grace::World::height * cell_size();
        offset_x = spare_x >= 0 ? spare_x / 2 : std::clamp(offset_x, spare_x, 0.0f);
        offset_y = spare_y >= 0 ? spare_y / 2 : std::clamp(offset_y, spare_y, 0.0f);
    }
    void pan(float dx, float dy) { offset_x += dx; offset_y += dy; clamp(); }
    void zoom_at(float amount, float screen_x, float screen_y) {
        const float old_size = cell_size();
        const float tile_x = (screen_x - margin - offset_x) / old_size;
        const float tile_y = (screen_y - header - offset_y) / old_size;
        zoom = std::clamp(zoom * amount, 0.65f, 2.25f);
        offset_x = screen_x - margin - tile_x * cell_size();
        offset_y = screen_y - header - tile_y * cell_size();
        clamp();
    }
    bool tile_at(float screen_x, float screen_y, int& x, int& y) const {
        if (screen_x < margin || screen_x >= margin + map_width ||
            screen_y < header || screen_y >= header + map_height) return false;
        x = static_cast<int>(std::floor((screen_x - margin - offset_x) / cell_size()));
        y = static_cast<int>(std::floor((screen_y - header - offset_y) / cell_size()));
        return x >= 0 && x < grace::World::width && y >= 0 && y < grace::World::height;
    }
    SDL_FRect tile_rect(int x, int y) const {
        return {margin + offset_x + x * cell_size(),
                header + offset_y + y * cell_size(), cell_size(), cell_size()};
    }
};

struct Color { Uint8 r, g, b; };

void fill(SDL_Renderer* renderer, SDL_FRect rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

void tint(SDL_Renderer* renderer, SDL_FRect rect, Color color, Uint8 alpha) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
    SDL_RenderFillRect(renderer, &rect);
}

using CoverageMask = std::array<bool, grace::World::width * grace::World::height>;

CoverageMask coverage_mask(const grace::World& world, grace::Tile service) {
    CoverageMask mask{};
    for (int source_y = 0; source_y < grace::World::height; ++source_y) {
        for (int source_x = 0; source_x < grace::World::width; ++source_x) {
            if (world.tile(source_x, source_y) == service &&
                world.is_active_service(source_x, source_y)) {
                for (int y = std::max(0, source_y - grace::World::service_radius);
                     y <= std::min(grace::World::height - 1, source_y + grace::World::service_radius); ++y) {
                    for (int x = std::max(0, source_x - grace::World::service_radius);
                         x <= std::min(grace::World::width - 1, source_x + grace::World::service_radius); ++x) {
                        if (grace::World::within_service_range(source_x, source_y, x, y))
                            mask[static_cast<std::size_t>(y * grace::World::width + x)] = true;
                    }
                }
            }
        }
    }
    return mask;
}

void draw_coverage(SDL_Renderer* renderer, const grace::World& world,
                   const grace::World* proposed,
                   grace::Tile service, const Camera& camera,
                   bool preview_valid) {
    const auto current_mask = coverage_mask(world, service);
    const auto future_mask = proposed ? coverage_mask(*proposed, service) : current_mask;
    for (int y = 0; y < grace::World::height; ++y) {
        for (int x = 0; x < grace::World::width; ++x) {
            const auto index = static_cast<std::size_t>(y * grace::World::width + x);
            const bool covered = current_mask[index];
            const bool future = future_mask[index];
            auto rect = camera.tile_rect(x, y);
            rect.x += 1; rect.y += 1; rect.w -= 2; rect.h -= 2;
            if (covered && future) tint(renderer, rect, {70, 190, 121}, 72);
            else if (!covered && future) tint(renderer, rect, {251, 191, 78}, 88);
            else if (covered && !future) tint(renderer, rect, {220, 80, 78}, 72);

            if (world.tile(x, y) == grace::Tile::home) {
                const bool served_now = world.home_has_service(x, y, service);
                const bool served_after = preview_valid && proposed
                    ? proposed->home_has_service(x, y, service) : served_now;
                const Color marker = !world.is_connected(x, y) ? Color{217, 76, 72} :
                    served_now && served_after ? Color{62, 211, 122} :
                    !served_now && served_after ? Color{255, 203, 94} :
                    Color{217, 76, 72};
                const float size = std::max(8.0f, camera.cell_size() * 0.28f);
                fill(renderer, {rect.x + rect.w - size - 2, rect.y + rect.h - size - 2,
                                size, size}, marker);
            }
        }
    }
}

void draw_tile(SDL_Renderer* renderer, int x, int y, grace::Tile type,
               grace::Terrain terrain,
               const Camera& camera, bool connected, bool active_service) {
    const auto tile_rect = camera.tile_rect(x, y);
    const float px = tile_rect.x;
    const float py = tile_rect.y;
    const float scale = camera.zoom;
    auto shape = [&](float rx, float ry, float width, float height, Color color) {
        fill(renderer, {px + rx * scale, py + ry * scale, width * scale, height * scale}, color);
    };
    const Color ground = terrain == grace::Terrain::water ? Color{74, 139, 166} :
        terrain == grace::Terrain::woods ? Color{82, 132, 82} :
        ((x + y) % 2 == 0) ? Color{123, 166, 102} : Color{129, 173, 107};
    shape(0, 0, 39, 39, ground);
    if (type == grace::Tile::meadow && terrain == grace::Terrain::woods) {
        shape(9, 18, 4, 14, {94, 76, 51});
        shape(24, 15, 4, 16, {94, 76, 51});
        shape(3, 4, 19, 21, {46, 106, 67});
        shape(18, 2, 18, 23, {55, 116, 72});
    }
    if (type == grace::Tile::meadow && terrain == grace::Terrain::water) {
        shape(5, 11, 13, 2, {135, 190, 201});
        shape(20, 25, 14, 2, {135, 190, 201});
    }
    switch (type) {
    case grace::Tile::road:
        shape(0, 12, 39, 16, {126, 119, 109});
        shape(16, 0, 8, 39, {126, 119, 109});
        break;
    case grace::Tile::home:
        shape(7, 15, 26, 19, {242, 221, 177});
        shape(4, 11, 32, 7, {154, 81, 66});
        shape(17, 23, 7, 11, {111, 81, 66});
        break;
    case grace::Tile::chapel:
        shape(6, 16, 28, 19, {245, 238, 211});
        shape(4, 12, 32, 6, {90, 105, 133});
        shape(18, 3, 4, 12, {90, 105, 133});
        shape(14, 6, 12, 3, {90, 105, 133});
        break;
    case grace::Tile::pantry:
        shape(6, 14, 28, 20, {217, 179, 122});
        shape(4, 10, 32, 7, {119, 91, 75});
        shape(14, 20, 12, 8, {88, 134, 110});
        break;
    case grace::Tile::park:
        shape(17, 20, 6, 16, {112, 80, 52});
        shape(9, 5, 22, 22, {55, 130, 79});
        break;
    default: break;
    }
    if (type != grace::Tile::meadow && !connected) {
        SDL_SetRenderDrawColor(renderer, 226, 75, 67, 255);
        const SDL_FRect warning{px + 2 * scale, py + 2 * scale,
                                35 * scale, 35 * scale};
        SDL_RenderRect(renderer, &warning);
    } else if ((type == grace::Tile::chapel || type == grace::Tile::pantry) &&
               !active_service) {
        SDL_SetRenderDrawColor(renderer, 255, 178, 74, 255);
        const SDL_FRect warning{px + 2 * scale, py + 2 * scale,
                                35 * scale, 35 * scale};
        SDL_RenderRect(renderer, &warning);
    }
}

const char* placement_message(grace::PlacementError error) {
    switch (error) {
    case grace::PlacementError::out_of_bounds: return "Choose a tile inside the valley.";
    case grace::PlacementError::occupied: return "That tile is occupied. Right click to clear it.";
    case grace::PlacementError::water: return "Water blocks construction. Build around the pond.";
    case grace::PlacementError::insufficient_funds: return "Not enough funds for that building.";
    case grace::PlacementError::needs_road: return "Build beside a connected road.";
    case grace::PlacementError::needs_connection: return "Extend the road from the starting chapel.";
    default: return "Ready to build.";
    }
}

std::string event_message(grace::EventError error, grace::EventKind kind,
                          int cooldown) {
    switch (error) {
    case grace::EventError::no_people: return "An event needs at least two residents.";
    case grace::EventError::cooldown:
        return "Another event is available in " + std::to_string(cooldown) + " weeks.";
    case grace::EventError::insufficient_funds: return kind == grace::EventKind::food_drive
        ? "A food drive costs 25 funds." : "A gathering costs 30 funds.";
    case grace::EventError::insufficient_volunteers: return "An event needs two free volunteers.";
    default: return kind == grace::EventKind::food_drive
        ? "Food drive started: extra care for two weeks."
        : "Gathering started: extra belonging for two weeks.";
    }
}

std::string town_advice(const grace::World& world) {
    const auto state = world.snapshot();
    int homes = 0;
    for (int y = 0; y < grace::World::height; ++y)
        for (int x = 0; x < grace::World::width; ++x)
            homes += world.tile(x, y) == grace::Tile::home;
    if (state.connected_homes < homes) return "Some homes lack a road route to the starting chapel.";
    if (state.treasury < 0) return "Funds are negative. Remove costly tiles, then let weekly support repay the debt.";
    if (state.weekly_upkeep > state.weekly_income)
        return "Weekly deficit: trim roads and upkeep, or grow a well-served neighborhood.";
    for (int y = 0; y < grace::World::height; ++y)
        for (int x = 0; x < grace::World::width; ++x)
            if ((world.tile(x, y) == grace::Tile::chapel ||
                 world.tile(x, y) == grace::Tile::pantry) &&
                world.is_connected(x, y) && !world.is_active_service(x, y))
                return "An orange service needs volunteers. Grow the town or press V.";
    if (state.care < 55) return "Care is falling. Staff a pantry or organize a food drive (F).";
    if (state.belonging < 55) return "Belonging is falling. Staff a chapel or host a gathering (E).";
    if (state.volunteers_used == state.volunteers_available)
        return "All volunteers are assigned. Grow the town before adding more services.";
    if (state.treasury < 80) return "Funds are low. Let weeks pass or add homes near services.";
    return "Grow connected homes, then check chapel and pantry coverage with tools 3 and 4.";
}

void title(SDL_Window* window, const grace::Snapshot& s, grace::Tile selected, bool paused) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "Grace Valley | Week %d%s | People %d/%d | Joy %d | Funds %d | %s (%d) | 1-5 build, right click remove, Space pause, N step",
                  s.week, paused ? " [paused]" : "", s.population, s.capacity,
                  s.happiness, s.treasury, grace::World::name(selected), grace::World::cost(selected));
    SDL_SetWindowTitle(window, buffer);
}

void autoplay_step(grace::World& world) {
    const int week = world.snapshot().week;
    if (week % 6 == 0 &&
        world.can_host_event(grace::EventKind::food_drive) == grace::EventError::none)
        world.host_event(grace::EventKind::food_drive);
    if (week == 2 && world.can_place(11, 9, grace::Tile::pantry))
        world.place(11, 9, grace::Tile::pantry);
    if (week % 4 == 0 && world.snapshot().capacity - world.snapshot().population <= 2) {
        for (int y = 0; y < grace::World::height; ++y)
            for (int x = 0; x < grace::World::width; ++x)
                if (world.can_place(x, y, grace::Tile::home)) {
                    world.place(x, y, grace::Tile::home);
                    return;
                }
    }
    if (week % 9 == 0) {
        for (int y = 0; y < grace::World::height; ++y)
            for (int x = 0; x < grace::World::width; ++x)
                if (world.can_place(x, y, grace::Tile::road)) {
                    world.place(x, y, grace::Tile::road);
                    return;
                }
    }
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    const bool capture = argc > 2 && std::string(argv[1]) == "--capture";
    const int soak_seconds = argc > 2 && std::string(argv[1]) == "--soak"
        ? std::max(1, std::atoi(argv[2])) : 0;
    int exit_status = 0;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Grace Valley", window_width, window_height,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, window_width, window_height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const std::string logo_path = std::string(SDL_GetBasePath()) +
        "assets/Grace Valley Sunrise Church Emblem.png";
    SDL_Surface* logo_surface = SDL_LoadPNG(logo_path.c_str());
    SDL_Texture* logo = logo_surface ? SDL_CreateTextureFromSurface(renderer, logo_surface) : nullptr;
    if (logo_surface) {
        SDL_SetWindowIcon(window, logo_surface);
        SDL_DestroySurface(logo_surface);
    }

    grace::World world;
    Camera camera;
    grace::Tile selected = capture ? grace::Tile::pantry : grace::Tile::road;
    bool running = true;
    bool paused = false;
    bool dragging = false;
    std::string feedback = "Welcome! Extend roads from the chapel, then build beside them.";
    Uint64 feedback_until = SDL_GetTicks() + 6000;
    Uint64 last_week = SDL_GetTicks();
    Uint64 last_frame = last_week;
    Uint64 last_autoplay = last_week;
    const Uint64 started = last_week;
    constexpr std::array<grace::Tile, 5> tools{
        grace::Tile::road, grace::Tile::home, grace::Tile::chapel,
        grace::Tile::pantry, grace::Tile::park};

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_MIDDLE) dragging = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key == SDLK_SPACE) paused = !paused;
                if (event.key.key == SDLK_N && paused) world.advance_week();
                if (event.key.key == SDLK_V) {
                    world.set_care_priority(!world.snapshot().care_first);
                    feedback = world.snapshot().care_first
                        ? "Volunteers now staff pantries before chapels."
                        : "Volunteers now staff chapels before pantries.";
                    feedback_until = SDL_GetTicks() + 4000;
                }
                if (event.key.key == SDLK_E) {
                    feedback = event_message(world.host_event(grace::EventKind::gathering),
                                             grace::EventKind::gathering,
                                             world.snapshot().event_cooldown);
                    feedback_until = SDL_GetTicks() + 4000;
                }
                if (event.key.key == SDLK_F) {
                    feedback = event_message(world.host_event(grace::EventKind::food_drive),
                                             grace::EventKind::food_drive,
                                             world.snapshot().event_cooldown);
                    feedback_until = SDL_GetTicks() + 4000;
                }
                if (event.key.key == SDLK_R) camera = Camera{};
                if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS)
                    camera.zoom_at(1.15f, window_width / 2.0f, header + map_height / 2.0f);
                if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                    camera.zoom_at(1.0f / 1.15f, window_width / 2.0f, header + map_height / 2.0f);
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_5)
                    selected = tools[static_cast<std::size_t>(event.key.key - SDLK_1)];
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
                camera.zoom_at(event.wheel.y > 0 ? 1.12f : 1.0f / 1.12f, mx, my);
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                float x = event.motion.x, y = event.motion.y;
                float old_x = x - event.motion.xrel, old_y = y - event.motion.yrel;
                SDL_RenderCoordinatesFromWindow(renderer, x, y, &x, &y);
                SDL_RenderCoordinatesFromWindow(renderer, old_x, old_y, &old_x, &old_y);
                camera.pan(x - old_x, y - old_y);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_MIDDLE) {
                    dragging = true;
                    continue;
                }
                float mx = event.button.x;
                float my = event.button.y;
                SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
                if (event.button.button == SDL_BUTTON_LEFT && my >= 17 && my < 59 &&
                    mx >= margin && mx < margin + 5 * 52) {
                    const int tool = static_cast<int>((mx - margin) / 52);
                    if (mx - margin - tool * 52 < 44) selected = tools[static_cast<std::size_t>(tool)];
                }
                int x, y;
                if (camera.tile_at(mx, my, x, y)) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        const auto error = world.placement_error(x, y, selected);
                        if (error == grace::PlacementError::none) {
                            const int price = world.cost_at(x, y, selected);
                            world.place(x, y, selected);
                            feedback = std::string(grace::World::name(selected)) +
                                " built for " + std::to_string(price) + " funds.";
                        } else feedback = placement_message(error);
                    }
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        if (world.remove(x, y)) feedback = "Tile cleared. Disconnected areas are marked red.";
                        else feedback = (x == 12 && y == 8)
                            ? "The chapel's starting road cannot be removed."
                            : "There is nothing to remove here.";
                    }
                    feedback_until = SDL_GetTicks() + 3500;
                }
            }
        }

        const Uint64 now = SDL_GetTicks();
        const float dt = std::min(0.05f, static_cast<float>(now - last_frame) / 1000.0f);
        last_frame = now;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const float movement = 300.0f * dt;
        camera.pan((keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? movement : 0.0f) -
                       (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? movement : 0.0f),
                   (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? movement : 0.0f) -
                       (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? movement : 0.0f));
        if (paused) last_week = now;
        if (!paused && now - last_week >= 2500) {
            world.advance_week();
            last_week = now;
        }
        if (soak_seconds > 0 && now - last_autoplay >= 2500) {
            autoplay_step(world);
            last_autoplay = now;
        }
        title(window, world.snapshot(), selected, paused);
        SDL_SetRenderDrawColor(renderer, 34, 51, 55, 255);
        SDL_RenderClear(renderer);

        // A small toolbar: colors correspond to the five numbered building tools.
        constexpr std::array<Color, 5> swatches{{
            {126, 119, 109}, {242, 221, 177}, {245, 238, 211},
            {217, 179, 122}, {55, 130, 79}}};
        for (std::size_t i = 0; i < tools.size(); ++i) {
            const float x = static_cast<float>(margin + static_cast<int>(i) * 52);
            fill(renderer, {x, 17, 44, 42}, tools[i] == selected ? Color{250, 227, 151} : Color{70, 85, 86});
            fill(renderer, {x + 5, 22, 34, 32}, swatches[i]);
            SDL_SetRenderDrawColor(renderer, 29, 44, 42, 255);
            const char key[] = {static_cast<char>('1' + i), '\0'};
            SDL_RenderDebugText(renderer, x + 18, 34, key);
        }
        const auto state = world.snapshot();
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        SDL_RenderDebugTextFormat(renderer, 300, 14, "WEEK %d%s  PEOPLE %d/%d  JOY %d",
                                  state.week, paused ? " PAUSED" : "", state.population,
                                  state.capacity, state.happiness);
        SDL_RenderDebugTextFormat(renderer, 300, 34,
            "FUNDS %d  IN %d  OUT %d  NET %+d  |  %s (%d)",
            state.treasury, state.weekly_income, state.weekly_upkeep,
            state.weekly_income - state.weekly_upkeep,
            grace::World::name(selected), grace::World::cost(selected));
        if (logo) {
            const SDL_FRect logo_rect{static_cast<float>(window_width - 82), 3, 64, 64};
            SDL_RenderTexture(renderer, logo, nullptr, &logo_rect);
        }
        SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
        SDL_RenderDebugText(renderer, margin, 76,
            "1-5 BUILD  3/4 COVERAGE  E GATHER  F FOOD  V PRIORITY  RIGHT CLEAR  SPACE PAUSE  WASD PAN  WHEEL ZOOM");
        const SDL_Rect map_clip{margin, header, map_width, map_height};
        SDL_SetRenderClipRect(renderer, &map_clip);
        for (int y = 0; y < grace::World::height; ++y)
            for (int x = 0; x < grace::World::width; ++x)
                draw_tile(renderer, x, y, world.tile(x, y), world.terrain(x, y),
                          camera, world.is_connected(x, y), world.is_active_service(x, y));

        float mx, my;
        if (capture) {
            mx = margin + 11.5f * base_cell;
            my = header + 9.5f * base_cell;
        } else {
            SDL_GetMouseState(&mx, &my);
            SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
        }
        int hover_x = -1, hover_y = -1;
        const bool over_tile = camera.tile_at(mx, my, hover_x, hover_y);
        const bool service_view = selected == grace::Tile::chapel ||
                                  selected == grace::Tile::pantry;
        const bool preview_valid = service_view && over_tile &&
                                   world.can_place(hover_x, hover_y, selected);
        std::optional<grace::World> preview;
        if (preview_valid) {
            preview = world;
            preview->place(hover_x, hover_y, selected);
        }
        if (service_view)
            draw_coverage(renderer, world, preview ? &*preview : nullptr,
                          selected, camera, preview_valid);
        if (over_tile) {
            const Color preview = world.can_place(hover_x, hover_y, selected)
                ? Color{253, 229, 153} : Color{225, 91, 83};
                SDL_SetRenderDrawColor(renderer, preview.r, preview.g, preview.b, 255);
                auto rect = camera.tile_rect(hover_x, hover_y);
                rect.x += 2; rect.y += 2; rect.w -= 4; rect.h -= 4;
                SDL_RenderRect(renderer, &rect);
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        fill(renderer, {0, static_cast<float>(header + map_height), window_width, 76},
             {34, 51, 55});
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        if (service_view) {
            SDL_RenderDebugTextFormat(renderer, margin, header + map_height + 10,
                "%s COVERAGE: %d/%d HOMES  |  PREVIEW %+d  |  GREEN COVERED  GOLD NEW  RED LOST/NEEDS",
                grace::World::name(selected), world.homes_with_service(selected),
                state.connected_homes,
                preview ? preview->homes_with_service(selected) -
                          world.homes_with_service(selected) : 0);
        } else {
            SDL_RenderDebugTextFormat(renderer, margin, header + map_height + 10,
                                      "ROADS %d CONNECTED  |  HOMES %d CONNECTED  |  SERVED %d",
                                      state.connected_roads, state.connected_homes, state.served_homes);
        }
        SDL_RenderDebugTextFormat(renderer, margin, header + map_height + 29,
            "BELONGING %d  CARE %d  |  VOLUNTEERS %d/%d  |  ACTIVE C/P %d/%d  |  V: %s  |  EVENT %s %dW CD%d",
            state.belonging, state.care, state.volunteers_used,
            state.volunteers_available, state.active_chapels, state.active_pantries,
            state.care_first ? "CARE" : "CHAPEL",
            state.event_weeks_left > 0
                ? (state.event_kind == grace::EventKind::food_drive ? "FOOD" : "GATHER")
                : "NONE", state.event_weeks_left, state.event_cooldown);
        std::string hint = now < feedback_until ? feedback :
            (over_tile ? placement_message(world.placement_error(hover_x, hover_y, selected)) :
                         town_advice(world));
        if (now >= feedback_until && over_tile && world.can_place(hover_x, hover_y, selected) &&
            world.terrain(hover_x, hover_y) == grace::Terrain::woods)
            hint = "Woods will be cleared: +10 funds. Total " +
                   std::to_string(world.cost_at(hover_x, hover_y, selected)) + ".";
        if (now >= feedback_until && over_tile &&
            world.tile(hover_x, hover_y) == grace::Tile::home) {
            const auto needs = world.household_needs(hover_x, hover_y);
            hint = "Home needs: belonging " + std::to_string(needs.belonging) +
                   ", care " + std::to_string(needs.care) +
                   (world.is_connected(hover_x, hover_y) ? "." : ". Road disconnected!");
        }
        if (now >= feedback_until && over_tile &&
            (world.tile(hover_x, hover_y) == grace::Tile::chapel ||
             world.tile(hover_x, hover_y) == grace::Tile::pantry) &&
            !world.is_active_service(hover_x, hover_y) &&
            world.is_connected(hover_x, hover_y))
            hint = "Orange service needs volunteers. Grow the town or press V to change priority.";
        if (now >= feedback_until && preview &&
            !preview->is_active_service(hover_x, hover_y))
            hint = "This building would lack volunteers. Grow the town or press V to change priority.";
        if (now >= feedback_until && !over_tile &&
            my >= 17 && my < 59 && mx >= margin && mx < margin + 5 * 52) {
            const int tool = static_cast<int>((mx - margin) / 52);
            if (mx - margin - tool * 52 < 44) {
                const auto type = tools[static_cast<std::size_t>(tool)];
                hint = std::to_string(tool + 1) + " " + grace::World::name(type) +
                       ": costs " + std::to_string(grace::World::cost(type)) + " funds.";
            }
        }
        SDL_RenderDebugText(renderer, margin, header + map_height + 51, hint.c_str());
        if (capture) {
            SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr);
            if (!frame || !SDL_SavePNG(frame, argv[2])) {
                std::fprintf(stderr, "Frame capture failed: %s\n", SDL_GetError());
                exit_status = 1;
            }
            if (frame) SDL_DestroySurface(frame);
        }
        SDL_RenderPresent(renderer);
        if (smoke || capture ||
            (soak_seconds > 0 && now - started >= static_cast<Uint64>(soak_seconds) * 1000))
            running = false;
        SDL_Delay(16);
    }

    if (logo) SDL_DestroyTexture(logo);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (soak_seconds > 0) {
        const auto state = world.snapshot();
        std::printf("soak seconds=%d week=%d population=%d joy=%d funds=%d events=%d\n",
                    soak_seconds, state.week, state.population, state.happiness,
                    state.treasury, state.events_hosted);
    }
    return exit_status;
}
