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
constexpr int window_height = map_height + header + 118;

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

void draw_tool_icon(SDL_Renderer* renderer, grace::Tile type, float x, float y,
                    char shortcut) {
    auto shape = [&](float dx, float dy, float w, float h, Color color) {
        fill(renderer, {x + dx, y + dy, w, h}, color);
    };
    shape(0, 0, 34, 32, {116, 159, 105});
    switch (type) {
    case grace::Tile::road:
        shape(0, 9, 34, 15, {188, 169, 135});
        shape(0, 12, 34, 9, {111, 111, 105});
        shape(5, 16, 7, 2, {231, 216, 171});
        shape(19, 16, 7, 2, {231, 216, 171});
        break;
    case grace::Tile::home:
    case grace::Tile::residential_zone:
        shape(7, 13, 20, 16, {242, 221, 177});
        shape(4, 9, 26, 6, {154, 81, 66});
        shape(15, 20, 6, 9, {111, 81, 66});
        shape(22, 6, 3, 5, {111, 81, 66});
        break;
    case grace::Tile::commercial_zone:
        shape(5, 13, 24, 16, {203, 226, 224});
        shape(4, 9, 26, 7, {61, 126, 139});
        shape(8, 9, 5, 7, {239, 218, 161});
        shape(19, 9, 5, 7, {239, 218, 161});
        shape(14, 20, 7, 9, {61, 126, 139});
        break;
    case grace::Tile::industrial_zone:
        shape(5, 16, 25, 13, {178, 151, 121});
        shape(4, 12, 26, 5, {97, 91, 86});
        shape(22, 4, 5, 9, {125, 102, 88});
        shape(9, 20, 5, 5, {239, 218, 161});
        break;
    case grace::Tile::chapel:
        shape(6, 14, 22, 15, {245, 238, 211});
        shape(4, 11, 26, 5, {90, 105, 133});
        shape(16, 3, 3, 9, {245, 238, 211});
        shape(12, 6, 11, 2, {245, 238, 211});
        shape(15, 21, 5, 8, {90, 105, 133});
        break;
    case grace::Tile::pantry:
        shape(6, 13, 22, 16, {217, 179, 122});
        shape(4, 9, 26, 6, {119, 91, 75});
        shape(11, 19, 12, 9, {88, 134, 110});
        shape(13, 21, 8, 2, {245, 238, 211});
        shape(13, 25, 8, 2, {245, 238, 211});
        break;
    case grace::Tile::park:
        shape(15, 17, 5, 12, {112, 80, 52});
        shape(8, 5, 19, 18, {55, 130, 79});
        shape(11, 8, 6, 4, {84, 158, 93});
        break;
    default: break;
    }
    shape(23, 21, 11, 11, {34, 51, 55});
    SDL_SetRenderDrawColor(renderer, 250, 238, 205, 255);
    const char label[]{shortcut, '\0'};
    SDL_RenderDebugText(renderer, x + 25, y + 23, label);
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

enum RoadSide : unsigned { north = 1, east = 2, south = 4, west = 8 };

unsigned road_sides(const grace::World& world, int x, int y) {
    return (world.tile(x, y - 1) == grace::Tile::road ? north : 0) |
           (world.tile(x + 1, y) == grace::Tile::road ? east : 0) |
           (world.tile(x, y + 1) == grace::Tile::road ? south : 0) |
           (world.tile(x - 1, y) == grace::Tile::road ? west : 0);
}

void draw_tile(SDL_Renderer* renderer, const grace::World& world, int x, int y,
               const Camera& camera) {
    const auto type = world.tile(x, y);
    const auto terrain = world.terrain(x, y);
    const bool connected = world.is_connected(x, y);
    const bool active_service = world.is_active_service(x, y);
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
    case grace::Tile::road: {
        const unsigned sides = road_sides(world, x, y);
        const Color curb{188, 169, 135};
        const Color street{111, 111, 105};
        const Color marking{231, 216, 171};
        shape(7, 7, 26, 26, curb);
        if (sides & north) shape(7, 0, 26, 20, curb);
        if (sides & east) shape(20, 7, 20, 26, curb);
        if (sides & south) shape(7, 20, 26, 20, curb);
        if (sides & west) shape(0, 7, 20, 26, curb);
        if ((sides & (north | east)) == (north | east))
            shape(27, 0, 13, 13, curb);
        if ((sides & (east | south)) == (east | south))
            shape(27, 27, 13, 13, curb);
        if ((sides & (south | west)) == (south | west))
            shape(0, 27, 13, 13, curb);
        if ((sides & (west | north)) == (west | north))
            shape(0, 0, 13, 13, curb);
        shape(10, 10, 20, 20, street);
        if (sides & north) shape(10, 0, 20, 20, street);
        if (sides & east) shape(20, 10, 20, 20, street);
        if (sides & south) shape(10, 20, 20, 20, street);
        if (sides & west) shape(0, 10, 20, 20, street);
        if ((sides & (east | west)) == (east | west) &&
            !(sides & (north | south))) {
            shape(3, 19, 7, 2, marking);
            shape(30, 19, 7, 2, marking);
        }
        if ((sides & (north | south)) == (north | south) &&
            !(sides & (east | west))) {
            shape(19, 3, 2, 7, marking);
            shape(19, 30, 2, 7, marking);
        }
        break;
    }
    case grace::Tile::home:
        shape(7, 15, 26, 19, {242, 221, 177});
        shape(4, 11, 32, 7, {154, 81, 66});
        shape(17, 23, 7, 11, {111, 81, 66});
        break;
    case grace::Tile::residential_zone:
        shape(3, 3, 34, 34, {161, 177, 113});
        shape(10, 18, 20, 15, {225, 215, 172});
        shape(8, 14, 24, 5, {154, 81, 66});
        break;
    case grace::Tile::commercial_zone:
        shape(3, 3, 34, 34, {102, 155, 164});
        shape(8, 17, 24, 16, {203, 226, 224});
        shape(6, 11, 28, 8, {61, 126, 139});
        shape(10, 11, 5, 8, {239, 218, 161});
        shape(22, 11, 5, 8, {239, 218, 161});
        break;
    case grace::Tile::industrial_zone:
        shape(3, 3, 34, 34, {166, 139, 110});
        shape(7, 18, 26, 15, {178, 151, 121});
        shape(6, 13, 28, 6, {97, 91, 86});
        shape(24, 5, 5, 9, {125, 102, 88});
        break;
    case grace::Tile::commercial:
        shape(6, 13, 28, 22, {219, 224, 213});
        shape(4, 9, 32, 8, {61, 126, 139});
        shape(9, 9, 6, 8, {239, 218, 161});
        shape(23, 9, 6, 8, {239, 218, 161});
        shape(16, 24, 8, 11, {61, 126, 139});
        break;
    case grace::Tile::industrial:
        shape(5, 18, 30, 17, {178, 151, 121});
        shape(4, 13, 32, 7, {97, 91, 86});
        shape(25, 5, 6, 10, {125, 102, 88});
        shape(10, 23, 6, 6, {239, 218, 161});
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
    const bool pending_zone = type == grace::Tile::residential_zone ||
        type == grace::Tile::commercial_zone ||
        type == grace::Tile::industrial_zone;
    if (type != grace::Tile::meadow && !pending_zone && !connected) {
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
    case grace::PlacementError::needs_connection: return "Build beside any existing road.";
    case grace::PlacementError::bankrupt: return "The town is bankrupt. Press Backspace to start again.";
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
    case grace::EventError::bankrupt: return "The town is bankrupt. Press Backspace to start again.";
    default: return kind == grace::EventKind::food_drive
        ? "Food drive started: extra care for two weeks."
        : "Gathering started: extra belonging for two weeks.";
    }
}

std::string town_advice(const grace::World& world) {
    const auto state = world.snapshot();
    if (state.bankrupt) return "BANKRUPT: the treasury fell below zero. Press Backspace to start again.";
    int homes = 0;
    for (int y = 0; y < grace::World::height; ++y)
        for (int x = 0; x < grace::World::width; ++x)
            homes += world.tile(x, y) == grace::Tile::home;
    if (state.connected_homes < homes) return "Some homes lack a road route to the starting chapel.";
    if (state.weekly_upkeep > state.weekly_income)
        return "Weekly deficit: press B for budget; adjust giving and services before funds run out.";
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
    if (state.treasury < 80) return "Funds are low. Check the budget (B) and zone homes near services.";
    if (state.capacity - state.population <= 2 && state.residential_demand >= 25)
        return "Housing demand is rising. Paint residential zones (2) beside connected roads.";
    if (state.commercial_demand >= 25 && state.shops == 0)
        return "Residents can support shops. Paint a commercial zone (3) beside a road.";
    return "Watch the demand bars, then inspect chapel and pantry coverage with tools 5 and 6.";
}

const char* giving_name(grace::GivingPolicy policy) {
    switch (policy) {
    case grace::GivingPolicy::gentle: return "GENTLE";
    case grace::GivingPolicy::ambitious: return "AMBITIOUS";
    default: return "BALANCED";
    }
}

const char* service_name(grace::ServiceBudget budget) {
    switch (budget) {
    case grace::ServiceBudget::lean: return "LEAN";
    case grace::ServiceBudget::generous: return "GENEROUS";
    default: return "BALANCED";
    }
}

const char* tax_name(grace::TaxPolicy policy) {
    switch (policy) {
    case grace::TaxPolicy::low: return "LOW";
    case grace::TaxPolicy::high: return "HIGH";
    default: return "BALANCED";
    }
}

void title(SDL_Window* window, const grace::Snapshot& s, grace::Tile selected,
           bool paused, bool started) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "Grace Valley | Week %d%s | People %d/%d | Joy %d | Funds %d | %s (%d) | Space play/pause, B budget",
                  s.week, s.bankrupt ? " [BANKRUPT]" : !started ? " [SETUP]" :
                  paused ? " [paused]" : "", s.population, s.capacity,
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

bool is_zone(grace::Tile type) {
    return type == grace::Tile::residential_zone ||
           type == grace::Tile::commercial_zone ||
           type == grace::Tile::industrial_zone;
}

struct PaintResult { int added; int x; int y; };

PaintResult paint_segment(grace::World& world, grace::Tile type,
                          int from_x, int from_y, int target_x, int target_y) {
    int x = from_x;
    int y = from_y;
    int added = 0;
    auto passable = [&](int next_x, int next_y) {
        return world.tile(next_x, next_y) == type ||
               world.can_place(next_x, next_y, type);
    };
    while (x != target_x || y != target_y) {
        const int dx = target_x - x;
        const int dy = target_y - y;
        const int step_x = dx > 0 ? 1 : -1;
        const int step_y = dy > 0 ? 1 : -1;
        const bool horizontal = dx != 0 && passable(x + step_x, y);
        const bool vertical = dy != 0 && passable(x, y + step_y);
        if (!horizontal && !vertical) break;
        if (horizontal && (!vertical || std::abs(dx) >= std::abs(dy))) x += step_x;
        else y += step_y;
        if (world.tile(x, y) != type && world.place(x, y, type)) ++added;
    }
    return {added, x, y};
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    const bool drag_smoke = argc > 1 && std::string(argv[1]) == "--drag-smoke";
    const bool zone_smoke = argc > 1 && std::string(argv[1]) == "--zone-smoke";
    const bool bankrupt_smoke = argc > 1 && std::string(argv[1]) == "--bankrupt-smoke";
    const bool capture_roads = argc > 2 && std::string(argv[1]) == "--capture-roads";
    const bool capture_budget = argc > 2 && std::string(argv[1]) == "--capture-budget";
    const bool capture_zones = argc > 2 && std::string(argv[1]) == "--capture-zones";
    const bool capture_bankrupt = argc > 2 && std::string(argv[1]) == "--capture-bankrupt";
    const bool capture = argc > 2 &&
        (std::string(argv[1]) == "--capture" || capture_roads ||
         capture_budget || capture_zones || capture_bankrupt);
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
    if (capture_bankrupt || bankrupt_smoke) {
        world.place(11, 9, grace::Tile::pantry);
        world.place(13, 7, grace::Tile::chapel);
        world.place(13, 9, grace::Tile::park);
        world.place(9, 7, grace::Tile::home);
        world.set_giving_policy(grace::GivingPolicy::gentle);
        world.set_service_budget(grace::ServiceBudget::generous);
        for (int week = 0; week < 30 && !world.snapshot().bankrupt; ++week)
            world.advance_week();
        if (!world.snapshot().bankrupt) {
            std::fprintf(stderr, "Could not set up bankruptcy UI check\n");
            exit_status = 1;
        }
    }
    if (capture_roads) {
        world.place(11, 9, grace::Tile::road);
        world.place(11, 10, grace::Tile::road);
        world.place(12, 10, grace::Tile::road);
        world.place(12, 9, grace::Tile::road);
        world.place(13, 9, grace::Tile::road);
    }
    if (capture_zones) {
        world.place(9, 7, grace::Tile::residential_zone);
        world.place(11, 9, grace::Tile::commercial_zone);
        world.place(13, 9, grace::Tile::industrial_zone);
        world.place(14, 9, grace::Tile::park);
        world.advance_week();
        world.advance_week();
        world.place(14, 7, grace::Tile::residential_zone);
        world.place(16, 8, grace::Tile::commercial_zone);
        world.place(15, 9, grace::Tile::industrial_zone);
    }
    Camera camera;
    grace::Tile selected = capture && !capture_roads && !capture_budget &&
        !capture_zones && !capture_bankrupt
        ? grace::Tile::pantry : grace::Tile::road;
    bool running = true;
    bool paused = soak_seconds == 0;
    bool simulation_started = soak_seconds > 0 || capture_zones ||
        capture_bankrupt || bankrupt_smoke;
    bool budget_open = capture_budget;
    bool dragging = false;
    bool painting_tiles = false;
    int paint_x = 0, paint_y = 0;
    std::string feedback = world.snapshot().bankrupt
        ? "BANKRUPT: review the budget or start a new town."
        : "Setup: build your town and press Space to start weekly cashflow. B opens the budget.";
    Uint64 feedback_until = SDL_GetTicks() + 6000;
    Uint64 last_week = SDL_GetTicks();
    Uint64 last_frame = last_week;
    Uint64 last_autoplay = last_week;
    const Uint64 started = last_week;
    constexpr std::array<grace::Tile, 7> tools{
        grace::Tile::road, grace::Tile::residential_zone,
        grace::Tile::commercial_zone, grace::Tile::industrial_zone,
        grace::Tile::chapel, grace::Tile::pantry, grace::Tile::park};
    auto new_town = [&] {
        world = grace::World{};
        camera = Camera{};
        selected = grace::Tile::road;
        simulation_started = false;
        paused = true;
        budget_open = false;
        dragging = false;
        painting_tiles = false;
        last_week = SDL_GetTicks();
        feedback = "New town ready. Press Space when your setup is complete.";
        feedback_until = SDL_GetTicks() + 4000;
    };
    if (bankrupt_smoke) {
        SDL_Event click{};
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button.button = SDL_BUTTON_LEFT;
        click.button.x = 370;
        click.button.y = 442;
        SDL_PushEvent(&click);
    }
    if (zone_smoke) {
        auto click = [&](float x, float y) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = x;
            event.button.y = y;
            SDL_PushEvent(&event);
        };
        SDL_Event tax{};
        tax.type = SDL_EVENT_KEY_DOWN;
        tax.key.key = SDLK_T;
        SDL_PushEvent(&tax);
        click(margin + 52 + 22, 38);
        click(margin + 9.5f * base_cell, header + 7.5f * base_cell);
        SDL_Event zone_drag{};
        zone_drag.type = SDL_EVENT_MOUSE_MOTION;
        zone_drag.motion.x = margin + 8.5f * base_cell;
        zone_drag.motion.y = header + 7.5f * base_cell;
        SDL_PushEvent(&zone_drag);
        click(margin + 2 * 52 + 22, 38);
        click(margin + 11.5f * base_cell, header + 9.5f * base_cell);
        click(margin + 3 * 52 + 22, 38);
        click(margin + 13.5f * base_cell, header + 9.5f * base_cell);
    }
    if (drag_smoke) {
        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = margin + 8.5f * base_cell;
        down.button.y = header + 9.5f * base_cell;
        SDL_PushEvent(&down);
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = margin + 13.5f * base_cell;
        motion.motion.y = down.button.y;
        motion.motion.xrel = 5 * base_cell;
        SDL_PushEvent(&motion);
        SDL_Event up{};
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        up.button.button = SDL_BUTTON_LEFT;
        up.button.x = motion.motion.x;
        up.button.y = motion.motion.y;
        SDL_PushEvent(&up);
        auto click = [&](Uint32 type, Uint8 button, int x, int y) {
            SDL_Event event{};
            event.type = type;
            event.button.button = button;
            event.button.x = margin + (x + 0.5f) * base_cell;
            event.button.y = header + (y + 0.5f) * base_cell;
            SDL_PushEvent(&event);
        };
        click(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 11, 9);
        click(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 11, 8);
        click(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 10, 8);
        SDL_Event repair_motion{};
        repair_motion.type = SDL_EVENT_MOUSE_MOTION;
        repair_motion.motion.x = margin + 10.5f * base_cell;
        repair_motion.motion.y = header + 10.5f * base_cell;
        SDL_PushEvent(&repair_motion);
        repair_motion.motion.x = margin + 11.5f * base_cell;
        SDL_PushEvent(&repair_motion);
        click(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 11, 10);
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_MIDDLE) dragging = false;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_LEFT) painting_tiles = false;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                dragging = false;
                painting_tiles = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key == SDLK_BACKSPACE) {
                    new_town();
                    continue;
                }
                if (event.key.key == SDLK_B) {
                    budget_open = !budget_open;
                    continue;
                }
                if (world.snapshot().bankrupt) continue;
                if (event.key.key == SDLK_SPACE) {
                    simulation_started = true;
                    paused = !paused;
                    feedback = paused ? "Simulation paused." : "Simulation running. Weekly cashflow has started.";
                    feedback_until = SDL_GetTicks() + 3500;
                }
                if (event.key.key == SDLK_N && paused && simulation_started &&
                    !world.snapshot().bankrupt) world.advance_week();
                if (event.key.key == SDLK_G && !world.snapshot().bankrupt) {
                    const int next = (static_cast<int>(world.snapshot().giving_policy) + 1) % 3;
                    world.set_giving_policy(static_cast<grace::GivingPolicy>(next));
                    feedback = std::string("Giving set to ") + giving_name(world.snapshot().giving_policy) + ".";
                    feedback_until = SDL_GetTicks() + 3500;
                }
                if (event.key.key == SDLK_O && !world.snapshot().bankrupt) {
                    const int next = (static_cast<int>(world.snapshot().service_budget) + 1) % 3;
                    world.set_service_budget(static_cast<grace::ServiceBudget>(next));
                    feedback = std::string("Services set to ") + service_name(world.snapshot().service_budget) + ".";
                    feedback_until = SDL_GetTicks() + 3500;
                }
                if (event.key.key == SDLK_T) {
                    const int next = (static_cast<int>(world.snapshot().tax_policy) + 1) % 3;
                    world.set_tax_policy(static_cast<grace::TaxPolicy>(next));
                    feedback = std::string("Business tax set to ") + tax_name(world.snapshot().tax_policy) +
                        ". Higher taxes raise revenue but slow new business growth.";
                    feedback_until = SDL_GetTicks() + 4500;
                }
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
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_7) {
                    selected = tools[static_cast<std::size_t>(event.key.key - SDLK_1)];
                    painting_tiles = false;
                }
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
            if (event.type == SDL_EVENT_MOUSE_MOTION && painting_tiles &&
                (selected == grace::Tile::road || is_zone(selected))) {
                float mx = event.motion.x, my = event.motion.y;
                SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
                int x, y;
                if (camera.tile_at(mx, my, x, y)) {
                    const int before = world.snapshot().treasury;
                    const auto result = paint_segment(world, selected, paint_x, paint_y, x, y);
                    paint_x = result.x;
                    paint_y = result.y;
                    if (result.added > 0) {
                        feedback = std::to_string(result.added) +
                                   (selected == grace::Tile::road ? " road tiles built for " :
                                    " zone tiles marked for ") +
                                   std::to_string(before - world.snapshot().treasury) + " funds.";
                        feedback_until = SDL_GetTicks() + 2500;
                    }
                }
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_MIDDLE &&
                    !world.snapshot().bankrupt) {
                    dragging = true;
                    continue;
                }
                float mx = event.button.x;
                float my = event.button.y;
                SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
                if (budget_open) {
                    if (event.button.button == SDL_BUTTON_RIGHT) budget_open = false;
                    if (event.button.button == SDL_BUTTON_LEFT &&
                        !world.snapshot().bankrupt) {
                        for (int choice = 0; choice < 3; ++choice) {
                            const float left = 205.0f + choice * 205.0f;
                            if (mx >= left && mx < left + 185.0f) {
                                if (my >= 250 && my < 284)
                                    world.set_giving_policy(static_cast<grace::GivingPolicy>(choice));
                                if (my >= 340 && my < 374)
                                    world.set_service_budget(static_cast<grace::ServiceBudget>(choice));
                                if (my >= 430 && my < 464)
                                    world.set_tax_policy(static_cast<grace::TaxPolicy>(choice));
                            }
                        }
                    }
                    continue;
                }
                if (world.snapshot().bankrupt) {
                    if (event.button.button == SDL_BUTTON_LEFT &&
                        my >= 420 && my < 466) {
                        if (mx >= 270 && mx < 480) new_town();
                        else if (mx >= 520 && mx < 730) budget_open = true;
                    }
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT && my >= 17 && my < 59 &&
                    mx >= margin && mx < margin + static_cast<int>(tools.size()) * 52) {
                    const int tool = static_cast<int>((mx - margin) / 52);
                    if (mx - margin - tool * 52 < 44) {
                        selected = tools[static_cast<std::size_t>(tool)];
                        painting_tiles = false;
                    }
                }
                int x, y;
                if (camera.tile_at(mx, my, x, y)) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        const auto error = world.placement_error(x, y, selected);
                        if ((selected == grace::Tile::road || is_zone(selected)) &&
                            world.tile(x, y) == selected) {
                            painting_tiles = true;
                            paint_x = x;
                            paint_y = y;
                            feedback = selected == grace::Tile::road
                                ? "Hold and drag to extend this road."
                                : "Hold and drag to extend this zone.";
                        } else if (error == grace::PlacementError::none) {
                            const int price = world.cost_at(x, y, selected);
                            world.place(x, y, selected);
                            if (selected == grace::Tile::road || is_zone(selected)) {
                                painting_tiles = true;
                                paint_x = x;
                                paint_y = y;
                            }
                            const bool zoning = selected == grace::Tile::residential_zone ||
                                selected == grace::Tile::commercial_zone ||
                                selected == grace::Tile::industrial_zone;
                            feedback = std::string(grace::World::name(selected)) +
                                (zoning ? " marked for " : " built for ") +
                                std::to_string(price) + " funds.";
                        } else feedback = placement_message(error);
                    }
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        painting_tiles = false;
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
            if (world.snapshot().bankrupt) {
                paused = true;
                budget_open = false;
                dragging = false;
                painting_tiles = false;
                feedback = "BANKRUPT: weekly expenses emptied the treasury. Press Backspace for a new town.";
                feedback_until = now + 10000;
            }
        }
        if (soak_seconds > 0 && now - last_autoplay >= 2500) {
            autoplay_step(world);
            last_autoplay = now;
        }
        title(window, world.snapshot(), selected, paused, simulation_started);
        SDL_SetRenderDrawColor(renderer, 34, 51, 55, 255);
        SDL_RenderClear(renderer);

        // Building silhouettes match the map tiles; small numbers retain shortcuts.
        for (std::size_t i = 0; i < tools.size(); ++i) {
            const float x = static_cast<float>(margin + static_cast<int>(i) * 52);
            fill(renderer, {x, 17, 44, 42}, tools[i] == selected ? Color{250, 227, 151} : Color{70, 85, 86});
            draw_tool_icon(renderer, tools[i], x + 5, 22,
                           static_cast<char>('1' + i));
        }
        const auto state = world.snapshot();
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        SDL_RenderDebugTextFormat(renderer, 400, 14, "WEEK %d %s  PEOPLE %d/%d  JOY %d",
                                  state.week, state.bankrupt ? "BANKRUPT" :
                                  !simulation_started ? "SETUP" : paused ? "PAUSED" : "RUNNING",
                                  state.population,
                                  state.capacity, state.happiness);
        SDL_RenderDebugTextFormat(renderer, 400, 34,
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
            state.bankrupt
                ? "BANKRUPT  |  CLICK NEW TOWN OR VIEW BUDGET  |  BACKSPACE RESTARTS  B OPENS BUDGET"
                : "SPACE PLAY/PAUSE  B BUDGET  T TAX  1-7 ZONE/BUILD  E/F EVENTS  RIGHT CLEAR  WASD PAN");
        const SDL_Rect map_clip{margin, header, map_width, map_height};
        SDL_SetRenderClipRect(renderer, &map_clip);
        for (int y = 0; y < grace::World::height; ++y)
            for (int x = 0; x < grace::World::width; ++x)
                draw_tile(renderer, world, x, y, camera);

        float mx, my;
        if (capture) {
            mx = margin + (capture_roads ? 10.5f : 11.5f) * base_cell;
            my = header + 9.5f * base_cell;
        } else {
            SDL_GetMouseState(&mx, &my);
            SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
        }
        int hover_x = -1, hover_y = -1;
        const bool over_tile = camera.tile_at(mx, my, hover_x, hover_y);
        const bool stroke_start = over_tile &&
            (selected == grace::Tile::road || is_zone(selected)) &&
            world.tile(hover_x, hover_y) == selected;
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
        if (selected == grace::Tile::road && over_tile &&
            world.can_place(hover_x, hover_y, selected)) {
            grace::World road_preview = world;
            road_preview.place(hover_x, hover_y, grace::Tile::road);
            constexpr std::array<std::array<int, 2>, 5> nearby{{
                {{0, 0}}, {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}}};
            for (const auto& offset : nearby) {
                const int x = hover_x + offset[0];
                const int y = hover_y + offset[1];
                if (road_preview.tile(x, y) == grace::Tile::road &&
                    x >= 0 && x < grace::World::width &&
                    y >= 0 && y < grace::World::height)
                    draw_tile(renderer, road_preview, x, y, camera);
            }
            auto ghost = camera.tile_rect(hover_x, hover_y);
            tint(renderer, ghost, {252, 210, 110}, 65);
        }
        if (over_tile) {
            const Color preview = (world.can_place(hover_x, hover_y, selected) || stroke_start)
                ? Color{253, 229, 153} : Color{225, 91, 83};
                SDL_SetRenderDrawColor(renderer, preview.r, preview.g, preview.b, 255);
                auto rect = camera.tile_rect(hover_x, hover_y);
                rect.x += 2; rect.y += 2; rect.w -= 4; rect.h -= 4;
                SDL_RenderRect(renderer, &rect);
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        if (budget_open) {
            fill(renderer, {170, 155, 660, 370}, {29, 45, 46});
            SDL_SetRenderDrawColor(renderer, 246, 221, 154, 255);
            SDL_RenderDebugText(renderer, 205, 175, "WEEKLY BUDGET  |  B TO CLOSE  |  CLICK A POLICY OR USE G / O / T");
            SDL_RenderDebugTextFormat(renderer, 205, 198,
                "IN: grant %d + giving %d + business tax %d = %d",
                state.grant_income, state.giving_income,
                state.tax_income, state.weekly_income);
            SDL_RenderDebugTextFormat(renderer, 205, 216,
                "OUT: roads/homes %d + services %d + community %d = %d    NET %+d",
                state.building_upkeep, state.service_upkeep,
                state.community_upkeep, state.weekly_upkeep,
                state.weekly_income - state.weekly_upkeep);
            SDL_RenderDebugText(renderer, 205, 237, "GIVING: more income can strain household wellbeing");
            constexpr std::array<const char*, 3> giving_labels{"GENTLE", "BALANCED", "AMBITIOUS"};
            constexpr std::array<const char*, 3> service_labels{"LEAN", "BALANCED", "GENEROUS"};
            constexpr std::array<const char*, 3> tax_labels{"LOW", "BALANCED", "HIGH"};
            for (int choice = 0; choice < 3; ++choice) {
                const float left = 205.0f + choice * 205.0f;
                fill(renderer, {left, 250, 185, 34},
                     static_cast<int>(state.giving_policy) == choice
                         ? Color{120, 106, 62} : Color{65, 80, 78});
                fill(renderer, {left, 340, 185, 34},
                     static_cast<int>(state.service_budget) == choice
                         ? Color{120, 106, 62} : Color{65, 80, 78});
                fill(renderer, {left, 430, 185, 34},
                     static_cast<int>(state.tax_policy) == choice
                         ? Color{120, 106, 62} : Color{65, 80, 78});
                SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
                SDL_RenderDebugText(renderer, left + 12, 262, giving_labels[choice]);
                SDL_RenderDebugText(renderer, left + 12, 352, service_labels[choice]);
                SDL_RenderDebugText(renderer, left + 12, 442, tax_labels[choice]);
            }
            SDL_RenderDebugText(renderer, 205, 287,
                "GENTLE 1/RES +1 NEEDS   BALANCED 1.5/RES   AMBITIOUS 2/RES -3 NEEDS");
            SDL_RenderDebugText(renderer, 205, 305, "SERVICES: higher upkeep improves care and belonging");
            SDL_RenderDebugText(renderer, 205, 377,
                "LEAN HALF COST +3 NEEDS   BALANCED +7   GENEROUS DOUBLE COST +11");
            SDL_RenderDebugText(renderer, 205, 398,
                "BUSINESS TAX: lower rates attract shops and workshops");
            SDL_RenderDebugText(renderer, 205, 472,
                "LOW: LESS REVENUE, MORE GROWTH    HIGH: MORE REVENUE, LESS GROWTH");
            SDL_RenderDebugText(renderer, 205, 501,
                state.bankrupt ? "BANKRUPT - BACKSPACE STARTS A NEW TOWN" :
                !simulation_started ? "SETUP - BUILD NOW, THEN PRESS SPACE TO START CASHFLOW" :
                "A NEGATIVE TREASURY AT WEEK END ENDS THIS TOWN");
        }
        if (state.bankrupt && !budget_open) {
            tint(renderer, {margin, header, map_width, map_height}, {12, 24, 28}, 175);
            fill(renderer, {220, 184, 560, 328}, {218, 113, 91});
            fill(renderer, {223, 187, 554, 322}, {29, 45, 46});
            fill(renderer, {245, 207, 510, 4}, {218, 113, 91});
            SDL_SetRenderDrawColor(renderer, 250, 230, 204, 255);
            SDL_SetRenderScale(renderer, 2.0f, 2.0f);
            SDL_RenderDebugText(renderer, 193, 114, "TOWN BANKRUPT");
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);
            SDL_RenderDebugText(renderer, 295, 268,
                "The treasury fell below zero after weekly expenses.");
            fill(renderer, {255, 302, 230, 83}, {55, 72, 70});
            fill(renderer, {515, 302, 230, 83}, {55, 72, 70});
            SDL_SetRenderDrawColor(renderer, 250, 230, 204, 255);
            SDL_RenderDebugTextFormat(renderer, 275, 319,
                "WEEK %d    PEOPLE %d", state.week, state.population);
            SDL_RenderDebugTextFormat(renderer, 275, 345,
                "FINAL FUNDS %d", state.treasury);
            SDL_RenderDebugTextFormat(renderer, 535, 319,
                "IN %d    OUT %d", state.weekly_income, state.weekly_upkeep);
            SDL_RenderDebugTextFormat(renderer, 535, 345,
                "NET %+d PER WEEK", state.weekly_income - state.weekly_upkeep);
            fill(renderer, {270, 420, 210, 46}, {176, 133, 73});
            fill(renderer, {520, 420, 210, 46}, {65, 91, 89});
            SDL_SetRenderDrawColor(renderer, 255, 244, 219, 255);
            SDL_RenderDebugText(renderer, 337, 438, "NEW TOWN");
            SDL_RenderDebugText(renderer, 577, 438, "VIEW BUDGET");
            SDL_RenderDebugText(renderer, 329, 481,
                "BACKSPACE: NEW TOWN    B: BUDGET");
        }
        fill(renderer, {0, static_cast<float>(header + map_height), window_width, 118},
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
        if (now >= feedback_until && over_tile && is_zone(selected) &&
            world.can_place(hover_x, hover_y, selected)) {
            const int demand = selected == grace::Tile::residential_zone
                ? state.residential_demand : selected == grace::Tile::commercial_zone
                    ? state.commercial_demand : state.industrial_demand;
            hint = std::string(grace::World::name(selected)) + ": costs " +
                   std::to_string(world.cost_at(hover_x, hover_y, selected)) +
                   ", demand " + std::to_string(demand) +
                   "/100. Needs a connected road and 25 demand to develop.";
        }
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
        if (now >= feedback_until && over_tile) {
            const auto hovered = world.tile(hover_x, hover_y);
            if (hovered == grace::Tile::residential_zone ||
                hovered == grace::Tile::commercial_zone ||
                hovered == grace::Tile::industrial_zone) {
                const int demand = hovered == grace::Tile::residential_zone
                    ? state.residential_demand : hovered == grace::Tile::commercial_zone
                        ? state.commercial_demand : state.industrial_demand;
                hint = std::string(grace::World::name(hovered)) +
                    (world.is_connected(hover_x, hover_y)
                        ? demand >= 25 ? ": ready to develop next week." :
                            ": waiting for demand to reach 25."
                        : ": waiting for a connected road.");
            } else if (hovered == grace::Tile::commercial ||
                       hovered == grace::Tile::industrial) {
                hint = std::string(grace::World::name(hovered)) +
                    (world.is_connected(hover_x, hover_y)
                        ? ": provides jobs and business tax revenue."
                        : ": disconnected from the road network.");
            }
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
            my >= 17 && my < 59 && mx >= margin &&
            mx < margin + static_cast<int>(tools.size()) * 52) {
            const int tool = static_cast<int>((mx - margin) / 52);
            if (mx - margin - tool * 52 < 44) {
                const auto type = tools[static_cast<std::size_t>(tool)];
                hint = std::to_string(tool + 1) + " " + grace::World::name(type) +
                       ": costs " + std::to_string(grace::World::cost(type)) + " funds.";
            }
        }
        SDL_RenderDebugText(renderer, margin, header + map_height + 51, hint.c_str());
        auto demand_bar = [&](int x, const char* label, int demand, Color color) {
            const float y = static_cast<float>(header + map_height + 79);
            SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
            SDL_RenderDebugText(renderer, static_cast<float>(x), y, label);
            fill(renderer, {static_cast<float>(x + 36), y, 190, 10}, {70, 85, 86});
            fill(renderer, {static_cast<float>(x + 36), y,
                            190.0f * demand / 100.0f, 10}, color);
            SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
            SDL_RenderDebugTextFormat(renderer, static_cast<float>(x + 236), y,
                                      "%3d", demand);
        };
        demand_bar(20, "RES", state.residential_demand, {105, 184, 114});
        demand_bar(340, "COM", state.commercial_demand, {91, 173, 187});
        demand_bar(660, "IND", state.industrial_demand, {218, 174, 103});
        SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
        SDL_RenderDebugTextFormat(renderer, margin, header + map_height + 99,
            "TAX %s (T)  |  SHOPS %d  WORKSHOPS %d  JOBS %d  |  ZONES NEED ROAD ACCESS AND DEMAND TO DEVELOP",
            tax_name(state.tax_policy), state.shops, state.workshops, state.jobs);
        if (capture) {
            SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr);
            if (!frame || !SDL_SavePNG(frame, argv[2])) {
                std::fprintf(stderr, "Frame capture failed: %s\n", SDL_GetError());
                exit_status = 1;
            }
            if (frame) SDL_DestroySurface(frame);
        }
        SDL_RenderPresent(renderer);
        if (smoke && (!paused || simulation_started || world.snapshot().week != 1)) {
            std::fprintf(stderr, "New game did not start in setup mode\n");
            exit_status = 1;
        }
        if (bankrupt_smoke && (world.snapshot().bankrupt ||
            world.snapshot().week != 1 || world.snapshot().treasury != 260 ||
            simulation_started || !paused || budget_open)) {
            std::fprintf(stderr, "Bankruptcy screen restart failed\n");
            exit_status = 1;
        }
        if (zone_smoke && (world.tile(9, 7) != grace::Tile::residential_zone ||
            world.tile(8, 7) != grace::Tile::residential_zone ||
            world.tile(11, 9) != grace::Tile::commercial_zone ||
            world.tile(13, 9) != grace::Tile::industrial_zone ||
            world.snapshot().tax_policy != grace::TaxPolicy::high ||
            world.snapshot().treasury != 217)) {
            std::fprintf(stderr, "Zoning toolbar or tax control failed\n");
            exit_status = 1;
        }
        if (drag_smoke) {
            for (int x = 8; x <= 13; ++x)
                if (x != 11 && world.tile(x, 9) != grace::Tile::road) exit_status = 1;
            if (world.tile(11, 8) != grace::Tile::meadow ||
                world.tile(10, 10) != grace::Tile::road ||
                world.tile(11, 10) != grace::Tile::road ||
                world.is_connected(10, 10) ||
                world.snapshot().treasury != 220) exit_status = 1;
            if (exit_status != 0)
                std::fprintf(stderr, "Road drag smoke test failed\n");
        }
        if (smoke || capture || drag_smoke || zone_smoke || bankrupt_smoke ||
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
