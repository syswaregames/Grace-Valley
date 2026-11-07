#include "grace/world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

constexpr float base_cell = 40.0f;
constexpr int margin = 20;
constexpr int header = 100;
// Keep the play viewport comfortable on a desktop display while the world
// itself can extend well beyond the initial view.
constexpr int map_width = 960;
constexpr int map_height = 640;
constexpr int footer_height = 58;
constexpr int sidebar_x = margin + map_width + 16;
constexpr int sidebar_width = 188;
constexpr int window_width = sidebar_x + sidebar_width + 20;
constexpr int window_height = map_height + header + footer_height;
constexpr SDL_FRect play_button{625, 17, 78, 42};
constexpr SDL_FRect budget_button{709, 17, 78, 42};
constexpr SDL_FRect reports_button{793, 17, 82, 42};
constexpr SDL_FRect options_button{881, 17, 82, 42};
constexpr SDL_FRect save_button{969, 17, 58, 42};
constexpr SDL_FRect load_button{1033, 17, 58, 42};
constexpr SDL_FRect reports_panel{75, 112, 870, 618};
constexpr SDL_FRect report_overview_tab{314, 118, 120, 28};
constexpr SDL_FRect report_power_tab{442, 118, 100, 28};
constexpr SDL_FRect traffic_alert_button{804, 72, 180, 24};
constexpr SDL_FRect advisor_panel{252, 220, 720, 330};
constexpr SDL_FRect advisor_loan_button{282, 468, 190, 44};
constexpr SDL_FRect advisor_budget_button{510, 468, 190, 44};
constexpr SDL_FRect advisor_resume_button{738, 468, 190, 44};
constexpr SDL_FRect budget_loan_button{663, 611, 202, 42};
constexpr SDL_FRect demand_item(int index) {
    return {static_cast<float>(20 + index * 98), 72, 92, 24};
}
constexpr SDL_FRect budget_choice(int row, int choice) {
    return {static_cast<float>(400 + choice * 153),
            static_cast<float>(270 + row * 89), 140, 42};
}
constexpr SDL_FRect upgrade_button{static_cast<float>(sidebar_x + 8), 620, 172, 35};
constexpr SDL_FRect hand_button{static_cast<float>(sidebar_x + 8), 466, 82, 35};
constexpr SDL_FRect bulldozer_button{static_cast<float>(sidebar_x + 96), 466, 84, 35};
constexpr SDL_FRect tool_button(int index) {
    return {static_cast<float>(sidebar_x + 8 + (index % 2) * 88),
            static_cast<float>(140 + (index / 2) * 82), 82, 76};
}
constexpr SDL_FRect option_button(int row) {
    return {static_cast<float>(sidebar_x + 8), static_cast<float>(190 + row * 48),
            172, 39};
}

bool replace_save_file(const std::filesystem::path& temporary,
                       const std::filesystem::path& destination) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

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
        zoom = std::clamp(zoom * amount, 0.35f, 2.25f);
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

struct FinanceSample { int week, funds, income, upkeep; };
enum class ReportTab { overview, power };

void fill(SDL_Renderer* renderer, SDL_FRect rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

void tint(SDL_Renderer* renderer, SDL_FRect rect, Color color, Uint8 alpha) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
    SDL_RenderFillRect(renderer, &rect);
}

bool contains(SDL_FRect rect, float x, float y) {
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

void draw_button(SDL_Renderer* renderer, SDL_FRect rect, const char* label,
                 bool active = false) {
    fill(renderer, rect, active ? Color{202, 169, 104} : Color{69, 92, 91});
    fill(renderer, {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4},
         active ? Color{120, 105, 65} : Color{46, 68, 68});
    SDL_SetRenderDrawColor(renderer, 247, 239, 213, 255);
    const float label_width = static_cast<float>(std::strlen(label) * 8);
    SDL_RenderDebugText(renderer, rect.x + (rect.w - label_width) / 2,
                        rect.y + (rect.h - 8) / 2, label);
}

void draw_budget_panel(SDL_Renderer* renderer, const grace::Snapshot& state) {
    tint(renderer, {margin, header, map_width, map_height}, {12, 20, 22}, 135);
    const SDL_FRect panel{140, 118, 760, 552};
    fill(renderer, panel, {29, 45, 46});
    SDL_SetRenderDrawColor(renderer, 202, 169, 104, 255);
    SDL_RenderRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
    SDL_RenderDebugText(renderer, 160, 135, "WEEKLY BUDGET");
    SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
    SDL_RenderDebugText(renderer, 727, 135, "B / ESC CLOSE");

    fill(renderer, {160, 160, 720, 82}, {43, 65, 65});
    SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
    SDL_RenderDebugText(renderer, 176, 171, "FUNDS");
    SDL_RenderDebugText(renderer, 346, 171, "IN / WEEK");
    SDL_RenderDebugText(renderer, 526, 171, "OUT / WEEK");
    SDL_RenderDebugText(renderer, 706, 171, "NET / WEEK");
    SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
    SDL_RenderDebugTextFormat(renderer, 176, 189, "%d", state.treasury);
    SDL_RenderDebugTextFormat(renderer, 346, 189, "+%d", state.weekly_income);
    SDL_RenderDebugTextFormat(renderer, 526, 189, "-%d", state.weekly_upkeep);
    const int net = state.weekly_income - state.weekly_upkeep;
    SDL_SetRenderDrawColor(renderer, net >= 0 ? 142 : 245,
        net >= 0 ? 207 : 130, net >= 0 ? 151 : 113, 255);
    SDL_RenderDebugTextFormat(renderer, 706, 189, "%+d", net);
    SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
    SDL_RenderDebugTextFormat(renderer, 176, 217, "Grant %d + giving %d + tax %d",
        state.grant_income, state.giving_income, state.tax_income);
    SDL_RenderDebugTextFormat(renderer, 526, 217,
        "Bld %d Road %d Svc %d Misc %d Loan %d",
        state.building_upkeep, state.road_upkeep,
        state.service_upkeep, state.community_upkeep,
        state.loan_due_this_week);

    constexpr std::array<const char*, 4> names{
        "GIVING", "SERVICES", "BUSINESS TAX", "ROAD CARE"};
    constexpr std::array<const char*, 4> descriptions{
        "Income vs wellbeing", "Care vs cost", "Revenue vs growth", "Repairs vs upkeep"};
    constexpr std::array<std::array<const char*, 3>, 4> choices{{
        {{"GENTLE", "BALANCED", "AMBITIOUS"}},
        {{"LEAN", "BALANCED", "GENEROUS"}},
        {{"LOW", "BALANCED", "HIGH"}},
        {{"LEAN", "BALANCED", "GENEROUS"}}}};
    constexpr std::array<std::array<const char*, 3>, 4> effects{{
        {{"Wellbeing +", "Steady", "Income +"}},
        {{"Lower cost", "Steady care", "Care +"}},
        {{"Growth +", "Steady", "Revenue +"}},
        {{"Road wear", "Maintain", "Repair +"}}}};
    const std::array<int, 4> selected{
        static_cast<int>(state.giving_policy), static_cast<int>(state.service_budget),
        static_cast<int>(state.tax_policy), static_cast<int>(state.road_budget)};
    for (int row = 0; row < 4; ++row) {
        const float top = 250.0f + row * 89.0f;
        fill(renderer, {160, top, 720, 80}, {36, 55, 55});
        fill(renderer, {160, top, 3, 80},
            row == 2 ? Color{91, 173, 187} :
            row == 3 ? Color{218, 174, 103} : Color{105, 184, 114});
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        SDL_RenderDebugText(renderer, 176, top + 16, names[static_cast<std::size_t>(row)]);
        SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
        SDL_RenderDebugText(renderer, 176, top + 38,
            descriptions[static_cast<std::size_t>(row)]);
        for (int choice = 0; choice < 3; ++choice) {
            const auto button = budget_choice(row, choice);
            const bool active = selected[static_cast<std::size_t>(row)] == choice;
            fill(renderer, button, active ? Color{120, 106, 62} : Color{59, 77, 75});
            if (active) {
                SDL_SetRenderDrawColor(renderer, 232, 190, 112, 255);
                SDL_RenderRect(renderer, &button);
            }
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugText(renderer, button.x + 10, button.y + 6,
                choices[static_cast<std::size_t>(row)][static_cast<std::size_t>(choice)]);
            SDL_SetRenderDrawColor(renderer, 196, 215, 200, 255);
            SDL_RenderDebugText(renderer, button.x + 10, button.y + 24,
                effects[static_cast<std::size_t>(row)][static_cast<std::size_t>(choice)]);
        }
    }
    fill(renderer, {160, 605, 720, 54}, {43, 65, 65});
    SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
    SDL_RenderDebugText(renderer, 176, 614, "COMMUNITY LOAN");
    SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
    if (state.loan_balance > 0)
        SDL_RenderDebugTextFormat(renderer, 176, 636,
            "Owed %d  |  Monthly %d (%d interest)", state.loan_balance,
            state.monthly_loan_payment, state.loan_interest);
    else SDL_RenderDebugText(renderer, 176, 636,
        "Borrow 200  |  Repay 20 + 5% interest each 4 weeks");
    draw_button(renderer, budget_loan_button,
        state.loan_balance > 0 ? "LOAN ACTIVE" : "BORROW 200",
        state.loan_balance > 0);
}

void draw_power_report(SDL_Renderer* renderer, const grace::World& world) {
    const auto state = world.snapshot();
    int zone_need = 0;
    int disconnected_zones = 0;
    for (int y = 0; y < grace::World::height; ++y) {
        for (int x = 0; x < grace::World::width; ++x) {
            const auto tile = world.tile(x, y);
            const int need = tile == grace::Tile::residential_zone ? 1 :
                tile == grace::Tile::commercial_zone ? 2 :
                tile == grace::Tile::industrial_zone ? 3 : 0;
            zone_need += need;
            if (need > 0 && !world.is_connected(x, y)) ++disconnected_zones;
        }
    }
    const int required = state.power_demand + zone_need;
    const int covered = std::min(required, state.power_capacity);
    const int shortage = std::max(0, required - state.power_capacity);
    const int surplus = std::max(0, state.power_capacity - required);
    const int unused_now = std::max(0, state.power_capacity - state.powered_demand);
    const auto text_color = [&](Uint8 r, Uint8 g, Uint8 b) {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    };
    fill(renderer, {95, 164, 830, 92}, {43, 65, 65});
    text_color(180, 202, 187);
    SDL_RenderDebugText(renderer, 112, 179, "TOTAL REQUIRED");
    SDL_RenderDebugText(renderer, 315, 179, "CAPACITY");
    SDL_RenderDebugText(renderer, 518, 179, "SHORTFALL");
    SDL_RenderDebugText(renderer, 715, 179, "SURPLUS");
    text_color(255, 239, 203);
    SDL_RenderDebugTextFormat(renderer, 112, 211, "%d kW", required);
    SDL_RenderDebugTextFormat(renderer, 315, 211, "%d kW", state.power_capacity);
    text_color(shortage > 0 ? 245 : 142, shortage > 0 ? 130 : 207,
               shortage > 0 ? 113 : 151);
    SDL_RenderDebugTextFormat(renderer, 518, 211, "%d kW", shortage);
    text_color(91, 173, 187);
    SDL_RenderDebugTextFormat(renderer, 715, 211, "%d kW", surplus);

    fill(renderer, {95, 274, 830, 220}, {36, 55, 55});
    text_color(245, 238, 211);
    SDL_RenderDebugText(renderer, 112, 292, "CAN YOUR POWER PLANTS COVER ALL ZONES?");
    text_color(180, 202, 187);
    SDL_RenderDebugTextFormat(renderer, 112, 323,
        "REQUIRED: %d kW  |  COVERED: %d kW  |  SHORT: %d kW  |  SURPLUS: %d kW",
        required, covered, shortage, surplus);
    fill(renderer, {112, 355, 680, 30}, {66, 82, 83});
    const int chart_max = std::max(required, state.power_capacity);
    if (chart_max > 0) {
        const float covered_width = 680.0f * covered / chart_max;
        const float shortage_width = 680.0f * shortage / chart_max;
        fill(renderer, {112, 355, covered_width, 30}, {105, 184, 114});
        fill(renderer, {112 + covered_width, 355, shortage_width, 30}, {215, 92, 76});
        fill(renderer, {112 + covered_width + shortage_width, 355,
                        680.0f * surplus / chart_max, 30}, {91, 173, 187});
    }
    text_color(142, 207, 151);
    SDL_RenderDebugText(renderer, 112, 402, "GREEN: COVERED");
    text_color(245, 150, 130);
    SDL_RenderDebugText(renderer, 340, 402, "RED: SHORTFALL");
    text_color(91, 173, 187);
    SDL_RenderDebugText(renderer, 570, 402, "BLUE: SURPLUS");
    text_color(180, 202, 187);
    SDL_RenderDebugText(renderer, 112, 452,
        "Estimate assumes every empty zone develops; zones do not use power yet.");

    fill(renderer, {95, 510, 830, 106}, {43, 65, 65});
    text_color(245, 238, 211);
    SDL_RenderDebugText(renderer, 112, 525, "WHERE THE NUMBERS COME FROM");
    SDL_RenderDebugTextFormat(renderer, 112, 551,
        "Buildings: %d kW  +  empty zones: %d kW  =  %d kW required",
        state.power_demand, zone_need, required);
    SDL_RenderDebugTextFormat(renderer, 112, 580,
        "IN USE: %d kW  |  UNUSED: %d kW  |  AFTER ZONES: %d kW",
        state.powered_demand, unused_now, surplus);

    fill(renderer, {95, 632, 830, 73}, {36, 55, 55});
    text_color(245, 238, 211);
    SDL_RenderDebugTextFormat(renderer, 112, 645,
        "Buildings without power: %d  |  Zones without road access: %d",
        state.unpowered_buildings, disconnected_zones);
    SDL_RenderDebugText(renderer, 112, 674,
        shortage > 0 ? "Add connected power plants to cover the projected shortfall." :
        state.unpowered_buildings > 0 || disconnected_zones > 0 ?
            "Check road connections: total capacity alone cannot reach disconnected tiles." :
            "Capacity covers current buildings and all planned zones.");
}

enum class ToolCategory { roads, zones, civic };

ToolCategory category_for(grace::Tile type) {
    if (type == grace::Tile::road) return ToolCategory::roads;
    if (type == grace::Tile::chapel || type == grace::Tile::pantry ||
        type == grace::Tile::park || type == grace::Tile::power_plant)
        return ToolCategory::civic;
    return ToolCategory::zones;
}

const char* tool_label(grace::Tile type) {
    switch (type) {
    case grace::Tile::road: return "ROAD";
    case grace::Tile::residential_zone: return "RES";
    case grace::Tile::commercial_zone: return "COM";
    case grace::Tile::industrial_zone: return "IND";
    case grace::Tile::chapel: return "CHAPEL";
    case grace::Tile::pantry: return "PANTRY";
    case grace::Tile::park: return "PARK";
    case grace::Tile::power_plant: return "POWER";
    default: return "TOOL";
    }
}

void draw_tool_icon(SDL_Renderer* renderer, grace::Tile type, float x, float y,
                    char shortcut) {
    auto shape = [&](float dx, float dy, float w, float h, Color color) {
        fill(renderer, {x + dx, y + dy, w, h}, color);
    };
    auto zone_icon = [&](Color border, Color lot, char symbol) {
        shape(3, 2, 28, 28, border);
        shape(6, 5, 22, 22, lot);
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        const char label[]{symbol, '\0'};
        SDL_RenderDebugText(renderer, x + 13, y + 12, label);
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
        shape(7, 13, 20, 16, {242, 221, 177});
        shape(4, 9, 26, 6, {154, 81, 66});
        shape(15, 20, 6, 9, {111, 81, 66});
        shape(22, 6, 3, 5, {111, 81, 66});
        break;
    case grace::Tile::residential_zone:
        zone_icon({105, 184, 114}, {117, 157, 102}, 'R');
        break;
    case grace::Tile::commercial_zone:
        zone_icon({91, 173, 187}, {115, 158, 146}, 'C');
        break;
    case grace::Tile::industrial_zone:
        zone_icon({218, 174, 103}, {158, 146, 108}, 'I');
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
    case grace::Tile::power_plant:
        shape(5, 12, 25, 18, {90, 112, 124});
        shape(8, 6, 5, 11, {179, 187, 177});
        shape(20, 4, 5, 13, {179, 187, 177});
        shape(14, 17, 7, 9, {244, 208, 93});
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
               const Camera& camera, Uint64 animation_ms) {
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
    auto vacant_zone = [&](Color border, Color lot, const char* label) {
        shape(3, 3, 34, 34, border);
        shape(6, 6, 28, 28, lot);
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        SDL_RenderDebugText(renderer, px + 16 * scale, py + 16 * scale, label);
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
        const bool bridge = terrain == grace::Terrain::water;
        const int level = world.road_level(x, y);
        const int traffic = world.traffic_at(x, y);
        const int capacity = world.road_capacity(x, y);
        const bool busy = traffic > capacity;
        const bool damaged = world.road_condition(x, y) < 60;
        const Color curb = damaged ? Color{186, 105, 78} : level == 0 ? Color{188, 169, 135} :
            level == 1 ? Color{162, 184, 188} :
            level == 2 ? Color{171, 185, 149} : Color{210, 176, 107};
        const Color street = busy ? (traffic > capacity * 2
            ? Color{172, 76, 68} : Color{176, 120, 63}) :
            damaged ? Color{119, 74, 66} :
            level == 0 ? Color{111, 111, 105} :
            level == 1 ? Color{76, 97, 110} :
            level == 2 ? Color{77, 91, 88} : Color{72, 77, 91};
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
        if (level > 0) {
            if (sides & (east | west)) shape(0, 19, 40, 2, {217, 226, 219});
            else shape(19, 0, 2, 40, {217, 226, 219});
        }
        if (level > 1) {
            const Color lane = level == 2 ? Color{194, 217, 184} : Color{248, 207, 110};
            if (sides & (east | west)) {
                shape(0, 14, 40, 1, lane);
                shape(0, 25, 40, 1, lane);
            } else {
                shape(14, 0, 1, 40, lane);
                shape(25, 0, 1, 40, lane);
            }
        }
        if (bridge) {
            const Color rail{233, 218, 179};
            if (sides & (east | west)) {
                shape(0, 5, 40, 3, rail);
                shape(0, 32, 40, 3, rail);
            }
            if (sides & (north | south)) {
                shape(5, 0, 3, 40, rail);
                shape(32, 0, 3, 40, rail);
            }
        }
        const int condition = world.road_condition(x, y);
        if (condition < 85 && scale >= 0.55f) {
            const bool horizontal = (sides & (east | west)) != 0;
            auto crack_line = [&](float x1, float y1, float x2, float y2) {
                SDL_SetRenderDrawColor(renderer, 54, 52, 50, 255);
                SDL_RenderLine(renderer, px + x1 * scale, py + y1 * scale,
                               px + x2 * scale, py + y2 * scale);
            };
            // Irregular branching fissures make deterioration read as surface
            // damage even when traffic and road-level colors are present.
            if (horizontal) {
                crack_line(13, 11, 16, 15);
                crack_line(16, 15, 14, 19);
                crack_line(14, 19, 18, 24);
            } else {
                crack_line(11, 13, 15, 16);
                crack_line(15, 16, 19, 14);
                crack_line(19, 14, 24, 18);
            }
            if (condition < 60) {
                if (horizontal) {
                    crack_line(25, 13, 22, 17);
                    crack_line(22, 17, 25, 21);
                    crack_line(7, 26, 11, 23);
                } else {
                    crack_line(13, 25, 17, 22);
                    crack_line(17, 22, 21, 25);
                    crack_line(26, 7, 23, 11);
                }
            }
            if (condition < 30) {
                const float pit_x = horizontal ? 17.0f : 12.0f;
                const float pit_y = horizontal ? 12.0f : 17.0f;
                const float pit_w = horizontal ? 8.0f : 5.0f;
                const float pit_h = horizontal ? 5.0f : 8.0f;
                shape(pit_x - 1, pit_y - 1, pit_w + 2, pit_h + 2,
                      {145, 115, 91});
                shape(pit_x, pit_y, pit_w, pit_h, {55, 53, 51});
                crack_line(pit_x + 1, pit_y + 1, pit_x + pit_w - 1,
                           pit_y + pit_h - 1);
                crack_line(pit_x + pit_w - 1, pit_y + 1, pit_x + 1,
                           pit_y + pit_h - 1);
            }
        }
        if (level > 0 && scale >= 0.85f) {
            shape(1, 1, 11, 11, level == 1 ? Color{76, 97, 110} :
                level == 2 ? Color{77, 105, 79} : Color{144, 106, 58});
            SDL_SetRenderDrawColor(renderer, 250, 239, 205, 255);
            const char badge[]{level == 1 ? 'A' : level == 2 ? 'B' : 'T', '\0'};
            SDL_RenderDebugText(renderer, px + 2 * scale, py + 2 * scale, badge);
        }
        if (damaged && scale >= 0.85f) {
            SDL_SetRenderDrawColor(renderer, 255, 220, 166, 255);
            SDL_RenderDebugText(renderer, px + 29 * scale, py + 2 * scale, "!");
        }
        if (traffic > 0) {
            const int count = std::min(traffic, 2);
            for (int car = 0; car < count; ++car) {
                const float position = static_cast<float>(
                    (animation_ms / 28 + x * 13 + y * 19 + car * 19) % 36);
                const Color body = car == 0 ? Color{242, 229, 182} : Color{87, 197, 200};
                if (sides & (east | west))
                    shape(position, car == 0 ? 12 : 24, 5, 3, body);
                else shape(car == 0 ? 12 : 24, position, 3, 5, body);
            }
        }
        break;
    }
    case grace::Tile::home:
        shape(7, 15, 26, 19, {242, 221, 177});
        shape(4, 11, 32, 7, {154, 81, 66});
        shape(17, 23, 7, 11, {111, 81, 66});
        break;
    case grace::Tile::residential_zone:
        vacant_zone({105, 184, 114}, {117, 157, 102}, "R");
        break;
    case grace::Tile::commercial_zone:
        vacant_zone({91, 173, 187}, {115, 158, 146}, "C");
        break;
    case grace::Tile::industrial_zone:
        vacant_zone({218, 174, 103}, {158, 146, 108}, "I");
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
    case grace::Tile::abandoned_home:
        shape(7, 17, 26, 17, {105, 100, 88});
        shape(4, 11, 32, 9, {83, 72, 66});
        shape(8, 23, 8, 8, {48, 54, 51});
        shape(23, 23, 8, 8, {48, 54, 51});
        shape(8, 25, 8, 2, {125, 91, 71});
        shape(23, 25, 8, 2, {125, 91, 71});
        shape(17, 27, 6, 7, {69, 63, 58});
        shape(2, 32, 7, 3, {79, 116, 66});
        shape(30, 33, 8, 3, {79, 116, 66});
        break;
    case grace::Tile::abandoned_commercial:
        shape(5, 16, 30, 19, {105, 104, 96});
        shape(4, 9, 32, 9, {76, 85, 84});
        shape(7, 20, 9, 10, {48, 54, 51});
        shape(24, 20, 9, 10, {48, 54, 51});
        shape(7, 23, 9, 2, {132, 91, 69});
        shape(24, 23, 9, 2, {132, 91, 69});
        shape(15, 18, 10, 3, {142, 109, 79});
        shape(2, 34, 8, 3, {79, 116, 66});
        break;
    case grace::Tile::abandoned_industrial:
        shape(5, 19, 30, 16, {109, 96, 84});
        shape(4, 13, 32, 8, {73, 72, 69});
        shape(25, 5, 6, 11, {89, 79, 70});
        shape(9, 23, 8, 8, {45, 50, 48});
        shape(23, 23, 8, 8, {45, 50, 48});
        shape(9, 26, 8, 2, {132, 91, 69});
        shape(23, 26, 8, 2, {132, 91, 69});
        shape(2, 34, 9, 3, {79, 116, 66});
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
    case grace::Tile::power_plant:
        shape(4, 16, 32, 19, {90, 112, 124});
        shape(8, 6, 6, 17, {176, 186, 181});
        shape(25, 3, 6, 20, {176, 186, 181});
        shape(17, 22, 7, 9, {245, 208, 86});
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
    const bool needs_power = type == grace::Tile::home ||
        type == grace::Tile::commercial || type == grace::Tile::industrial ||
        pending_zone;
    if (needs_power && !world.is_powered(x, y)) {
        // Amber lightning badge: a vacant zone cannot develop, or an existing
        // building is offline, until a road-connected plant has spare output.
        shape(25, 1, 14, 16, {224, 145, 48});
        shape(32, 3, 3, 5, {255, 244, 205});
        shape(29, 8, 6, 3, {255, 244, 205});
        shape(29, 10, 3, 5, {255, 244, 205});
    }
    if (world.is_noise_polluted(x, y) && scale >= 0.85f) {
        shape(1, 1, 12, 12, {143, 75, 142});
        SDL_SetRenderDrawColor(renderer, 255, 237, 250, 255);
        SDL_RenderDebugText(renderer, px + 3 * scale, py + 3 * scale, "N");
    }
}

const char* placement_message(grace::PlacementError error) {
    switch (error) {
    case grace::PlacementError::out_of_bounds: return "Choose a tile inside the valley.";
    case grace::PlacementError::occupied: return "That tile is occupied. Right click to clear it.";
    case grace::PlacementError::water: return "Buildings need land. Use the Road tool to bridge water.";
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

struct TrafficTip {
    int x = 0;
    int y = 0;
    int flow = 0;
    int capacity = 0;
    const char* action = "Add a second connected road to split trips.";
};

std::optional<TrafficTip> traffic_tip(const grace::World& world) {
    std::optional<TrafficTip> worst;
    int worst_pressure = 0;
    for (int y = 0; y < grace::World::height; ++y) {
        for (int x = 0; x < grace::World::width; ++x) {
            if (world.tile(x, y) != grace::Tile::road) continue;
            const int flow = world.traffic_at(x, y);
            const int capacity = world.road_capacity(x, y);
            if (capacity <= 0 || flow <= capacity) continue;
            const int pressure = flow * 100 / capacity;
            if (pressure <= worst_pressure) continue;
            worst_pressure = pressure;
            worst = TrafficTip{x, y, flow, capacity};
        }
    }
    if (!worst) return worst;
    if (world.road_condition(worst->x, worst->y) < 60) {
        worst->action = "Fund road care in Budget to restore this road's capacity.";
        return worst;
    }
    bool near_homes = false;
    bool near_jobs = false;
    bool near_industry = false;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            const auto tile = world.tile(worst->x + dx, worst->y + dy);
            near_homes |= tile == grace::Tile::home ||
                tile == grace::Tile::residential_zone;
            near_jobs |= tile == grace::Tile::commercial ||
                tile == grace::Tile::industrial;
            near_industry |= tile == grace::Tile::industrial ||
                tile == grace::Tile::industrial_zone;
        }
    }
    if (near_homes && near_jobs)
        worst->action = "Connect a second route between nearby homes and jobs.";
    else if (near_industry)
        worst->action = "Build a parallel route around the workshop district.";
    return worst;
}

std::string town_advice(const grace::World& world) {
    const auto state = world.snapshot();
    if (state.bankrupt) return "BANKRUPT: the treasury fell below zero. Press Backspace to start again.";
    if (state.abandoned_buildings > 0)
        return std::to_string(state.abandoned_buildings) +
            " buildings abandoned. Raise City Score above 45 for three weeks to restore them.";
    if (state.city_score < 30)
        return "City Score is critically low. If it stays below 30, buildings will be abandoned.";
    if (state.power_capacity == 0)
        return "No power: build a power plant (8) beside a connected road to grow zones.";
    if (state.unpowered_buildings > 0)
        return "Power shortage: add a connected plant. Amber lightning marks unpowered tiles.";
    if (const auto traffic = traffic_tip(world))
        return "Traffic at (" + std::to_string(traffic->x + 1) + "," +
            std::to_string(traffic->y + 1) + "): " + traffic->action;
    if (state.noise_affected_homes > 0)
        return "Noise alert: workshops affect " +
            std::to_string(state.noise_affected_homes) +
            " homes. Move housing away from workshops and power plants.";
    if (state.bad_roads > 0)
        return "Roads are deteriorating. Open Budget (B) and restore road maintenance funding.";
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

std::string financial_advice(const grace::World& world) {
    const auto state = world.snapshot();
    if (state.loan_balance > 0 &&
        state.weekly_income >= state.weekly_upkeep - state.loan_due_this_week)
        return "A loan payment is near. Preserve funds and lower costs until the balance falls.";
    if (state.weekly_upkeep <= state.weekly_income)
        return "Your income currently covers weekly costs. Keep a cash reserve before expanding.";
    if (state.giving_policy == grace::GivingPolicy::ambitious)
        return "Giving is ambitious. Set it to Balanced or Gentle in Budget to reduce the weekly deficit.";
    if (state.service_budget == grace::ServiceBudget::generous)
        return "Services are generously funded. Try Balanced funding, then watch care and belonging.";
    if (state.road_budget == grace::RoadBudget::generous && state.bad_roads == 0)
        return "Roads are healthy. Balanced maintenance can lower costs; restore funding if roads wear down.";
    if (state.tax_policy != grace::TaxPolicy::high)
        return "Consider High business tax for more weekly revenue, then check whether business demand slows.";
    return "Pause expansion and use Budget to trim upkeep. Protect essential care, power, and road access.";
}

struct MapTooltip {
    std::string title;
    std::array<std::string, 3> details{};
    int detail_count = 0;
};

MapTooltip map_tooltip(const grace::World& world, int x, int y) {
    const auto tile = world.tile(x, y);
    const auto state = world.snapshot();
    MapTooltip tip;
    tip.title = grace::World::name(tile);
    auto add = [&](std::string line) {
        if (tip.detail_count < static_cast<int>(tip.details.size()))
            tip.details[tip.detail_count++] = std::move(line);
    };
    switch (tile) {
    case grace::Tile::home: {
        const auto needs = world.household_needs(x, y);
        add("Belonging " + std::to_string(needs.belonging) +
            "   Care " + std::to_string(needs.care));
        add(std::string("Power ") + (world.is_powered(x, y) ? "connected" : "unpowered") +
            (world.is_connected(x, y) ? " | Road connected" : " | Road isolated"));
        add(std::string("Chapel ") + (world.home_has_service(x, y, grace::Tile::chapel)
            ? "yes" : "no") + " | Pantry " +
            (world.home_has_service(x, y, grace::Tile::pantry) ? "yes" : "no") +
            (world.is_noise_polluted(x, y) ? " | Noise" : ""));
        break;
    }
    case grace::Tile::road: {
        const int condition = world.road_condition(x, y);
        add("Traffic " + std::to_string(world.traffic_at(x, y)) + "/" +
            std::to_string(world.road_capacity(x, y)) + " | Condition " +
            std::to_string(condition) + "%");
        add(std::string(condition < 30 ? "Severely damaged" : condition < 60
            ? "Damaged; maintenance recommended" : "Maintained") +
            (world.terrain(x, y) == grace::Terrain::water ? " | Bridge" : ""));
        add(std::string(grace::World::road_level_name(world.road_level(x, y))) +
            (world.next_road_upgrade_cost(x, y) > 0
                ? " | Upgrade " + std::to_string(world.next_road_upgrade_cost(x, y))
                : " | Max level"));
        break;
    }
    case grace::Tile::residential_zone:
    case grace::Tile::commercial_zone:
    case grace::Tile::industrial_zone: {
        const int demand = tile == grace::Tile::residential_zone
            ? state.residential_demand : tile == grace::Tile::commercial_zone
                ? state.commercial_demand : state.industrial_demand;
        const int power_need = tile == grace::Tile::residential_zone ? 1 :
            tile == grace::Tile::commercial_zone ? 2 : 3;
        add("Demand " + std::to_string(demand) + "/100 | Potential " +
            std::to_string(power_need) + " kW");
        add(std::string("Road ") + (world.is_connected(x, y)
            ? "connected" : "not connected") + " | Power " +
            (world.is_powered(x, y) ? "available" : "unavailable"));
        add(demand >= 25 ? "Can grow when weekly simulation runs" :
                           "Waiting for demand to reach 25");
        break;
    }
    case grace::Tile::commercial:
        add("4 jobs | Tax " + std::to_string(state.tax_policy == grace::TaxPolicy::high
            ? 9 : state.tax_policy == grace::TaxPolicy::low ? 3 : 6) + "/week");
        add(world.is_powered(x, y) ? "Powered and operating" : "Unpowered; no jobs or tax");
        add("Connected road: " + std::string(world.is_connected(x, y) ? "yes" : "no"));
        break;
    case grace::Tile::industrial:
        add("6 jobs | Tax " + std::to_string(state.tax_policy == grace::TaxPolicy::high
            ? 14 : state.tax_policy == grace::TaxPolicy::low ? 5 : 9) + "/week");
        add(world.is_powered(x, y) ? "Powered and operating" : "Unpowered; no jobs or tax");
        add("Workshop noise can affect nearby homes");
        break;
    case grace::Tile::chapel:
    case grace::Tile::pantry:
        add(world.is_active_service(x, y) ? "Staffed and serving" : "Needs volunteers");
        add("Serving " + std::to_string(world.homes_with_service(tile)) + " homes");
        add("Volunteers used " + std::to_string(state.volunteers_used) + "/" +
            std::to_string(state.volunteers_available));
        break;
    case grace::Tile::park:
        add(world.is_connected(x, y) ? "Connected public green space" : "Road access needed");
        add("Improves nearby wellbeing and housing demand");
        break;
    case grace::Tile::power_plant:
        add("Supply " + std::to_string(state.power_capacity) +
            " kW | Building demand " + std::to_string(state.power_demand) + " kW");
        add(world.is_connected(x, y) ? "Connected to road power grid" : "Disconnected from grid");
        add("12 kW | Noise and pollution affect nearby homes");
        break;
    case grace::Tile::abandoned_home:
    case grace::Tile::abandoned_commercial:
    case grace::Tile::abandoned_industrial:
        add("No housing capacity, jobs, or tax income");
        add("City Score above 45 for three weeks to reopen");
        add("Right click to clear this property");
        break;
    default:
        add(world.terrain(x, y) == grace::Terrain::woods ? "Woodland" :
            world.terrain(x, y) == grace::Terrain::water ? "Water" : "Open land");
        add("Select a toolbar item to build or zone here");
        break;
    }
    return tip;
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

const char* road_budget_name(grace::RoadBudget budget) {
    switch (budget) {
    case grace::RoadBudget::lean: return "LEAN";
    case grace::RoadBudget::generous: return "GENEROUS";
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

struct PaintResult { int added; int bridges; int x; int y; };

PaintResult paint_segment(grace::World& world, grace::Tile type,
                          int from_x, int from_y, int target_x, int target_y) {
    int x = from_x;
    int y = from_y;
    int added = 0;
    int bridges = 0;
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
        if (world.tile(x, y) != type) {
            const bool over_water = type == grace::Tile::road &&
                world.terrain(x, y) == grace::Terrain::water;
            if (world.place(x, y, type)) {
                ++added;
                bridges += over_water;
            }
        }
    }
    return {added, bridges, x, y};
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    const bool drag_smoke = argc > 1 && std::string(argv[1]) == "--drag-smoke";
    const bool pan_smoke = argc > 1 && std::string(argv[1]) == "--pan-smoke";
    const bool bridge_smoke = argc > 1 && std::string(argv[1]) == "--bridge-smoke";
    const bool zone_smoke = argc > 1 && std::string(argv[1]) == "--zone-smoke";
    const bool toolbar_smoke = argc > 1 && std::string(argv[1]) == "--toolbar-smoke";
    const bool power_smoke = argc > 1 && std::string(argv[1]) == "--power-smoke";
    const bool traffic_smoke = argc > 1 && std::string(argv[1]) == "--traffic-smoke";
    const bool save_smoke = argc > 2 && std::string(argv[1]) == "--save-smoke";
    const bool bankrupt_smoke = argc > 1 && std::string(argv[1]) == "--bankrupt-smoke";
    const bool capture_roads = argc > 2 && std::string(argv[1]) == "--capture-roads";
    const bool capture_bridge = argc > 2 && std::string(argv[1]) == "--capture-bridge";
    const bool capture_budget = argc > 2 && std::string(argv[1]) == "--capture-budget";
    const bool capture_reports = argc > 2 && std::string(argv[1]) == "--capture-reports";
    const bool capture_power_surplus = argc > 2 && std::string(argv[1]) == "--capture-power-surplus";
    const bool capture_power = capture_power_surplus ||
        (argc > 2 && std::string(argv[1]) == "--capture-power");
    const bool capture_options = argc > 2 && std::string(argv[1]) == "--capture-options";
    const bool capture_zones = argc > 2 && std::string(argv[1]) == "--capture-zones";
    const bool capture_traffic = argc > 2 && std::string(argv[1]) == "--capture-traffic";
    const bool capture_bankrupt = argc > 2 && std::string(argv[1]) == "--capture-bankrupt";
    const bool capture = argc > 2 &&
        (std::string(argv[1]) == "--capture" || capture_roads || capture_bridge ||
         capture_budget || capture_reports || capture_power || capture_options || capture_zones || capture_traffic ||
         capture_bankrupt);
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
    std::filesystem::path save_file;
    if (save_smoke) save_file = std::filesystem::path(argv[2]);
    else if (char* preference_path = SDL_GetPrefPath("SyswareGames", "GraceValley")) {
        save_file = std::filesystem::u8path(preference_path) / "grace-valley.sav";
        SDL_free(preference_path);
    }
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
        world.upgrade_road(10, 8);
        world.upgrade_road(11, 8);
        world.upgrade_road(11, 8);
        world.upgrade_road(12, 8);
        world.upgrade_road(12, 8);
        world.upgrade_road(12, 8);
    }
    if (capture_bridge) {
        for (int x : {16, 17}) world.place(x, 8, grace::Tile::road);
        for (int y : {7, 6, 5, 4}) world.place(17, y, grace::Tile::road);
        for (int x = 18; x <= 23; ++x) world.place(x, 4, grace::Tile::road);
    }
    if (capture_power) {
        world.place(13, 9, grace::Tile::power_plant);
        if (!capture_power_surplus)
            for (int x = 8; x <= 13; ++x)
                world.place(x, 7, grace::Tile::industrial_zone);
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
    if (capture_traffic || traffic_smoke) {
        world.place(15, 7, grace::Tile::power_plant);
        world.place(9, 7, grace::Tile::home);
        world.place(8, 7, grace::Tile::home);
        world.place(13, 9, grace::Tile::industrial_zone);
        world.place(14, 9, grace::Tile::commercial_zone);
        world.place(15, 9, grace::Tile::commercial_zone);
        for (int week = 0; week < 12; ++week) world.advance_week();
    }
    Camera camera;
    grace::Tile selected = capture_zones ? grace::Tile::residential_zone :
        capture && !capture_roads && !capture_bridge && !capture_budget &&
        !capture_options && !capture_traffic && !capture_bankrupt
            ? grace::Tile::pantry : grace::Tile::road;
    bool running = true;
    bool paused = soak_seconds == 0;
    bool simulation_started = soak_seconds > 0 || capture_zones || capture_traffic ||
        capture_bankrupt || bankrupt_smoke;
    bool budget_open = capture_budget;
    bool reports_open = capture_reports || capture_power;
    ReportTab report_tab = capture_power ? ReportTab::power : ReportTab::overview;
    bool options_open = capture_options;
    bool upgrade_mode = false;
    bool bulldozer_mode = false;
    bool hand_mode = false;
    bool dragging = false;
    Uint8 pan_button = 0;
    bool painting_tiles = false;
    bool advisor_open = false;
    bool advisor_acknowledged = false;
    std::vector<FinanceSample> finance_history;
    int paint_x = 0, paint_y = 0;
    const auto record_finances = [&] {
        const auto snapshot = world.snapshot();
        finance_history.push_back({snapshot.week, snapshot.treasury,
                                   snapshot.weekly_income, snapshot.weekly_upkeep});
        if (finance_history.size() > 12) finance_history.erase(finance_history.begin());
    };
    record_finances();
    std::string feedback = capture_traffic
        ? "Traffic alert: try a second route. Click the top alert for a location and tip."
        : world.snapshot().bankrupt
        ? "BANKRUPT: review the budget or start a new town."
        : "Setup: build a power plant (8) by a connected road, then press Space to start.";
    Uint64 feedback_until = SDL_GetTicks() + 6000;
    Uint64 last_week = SDL_GetTicks();
    Uint64 last_frame = last_week;
    Uint64 car_animation_ms = 0;
    Uint64 last_autoplay = last_week;
    const Uint64 started = last_week;
    bool hover_tracking = false;
    int hover_track_x = -1, hover_track_y = -1;
    float hover_anchor_x = 0, hover_anchor_y = 0;
    Uint64 hover_started = 0;
    int demand_hover = -1;
    Uint64 demand_hover_started = 0;
    constexpr std::array<grace::Tile, 8> tools{
        grace::Tile::road, grace::Tile::residential_zone,
        grace::Tile::commercial_zone, grace::Tile::industrial_zone,
        grace::Tile::chapel, grace::Tile::pantry, grace::Tile::park,
        grace::Tile::power_plant};
    auto new_town = [&] {
        world = grace::World{};
        camera = Camera{};
        selected = grace::Tile::road;
        simulation_started = false;
        paused = true;
        budget_open = false;
        reports_open = false;
        report_tab = ReportTab::overview;
        options_open = false;
        upgrade_mode = false;
        bulldozer_mode = false;
        hand_mode = false;
        advisor_open = false;
        advisor_acknowledged = false;
        finance_history.clear();
        record_finances();
        dragging = false;
        pan_button = 0;
        painting_tiles = false;
        last_week = SDL_GetTicks();
        feedback = "New town ready. Build a power plant (8), then press Space.";
        feedback_until = SDL_GetTicks() + 4000;
    };
    auto toggle_play = [&] {
        if (world.snapshot().bankrupt) return;
        simulation_started = true;
        paused = !paused;
        feedback = paused ? "Simulation paused." : "Simulation running. Weekly cashflow has started.";
        feedback_until = SDL_GetTicks() + 3500;
    };
    auto borrow_money = [&] {
        if (world.take_loan())
            feedback = "Loan received: 200 funds. Repay 20 plus 5% interest every four weeks.";
        else feedback = "A loan is already active. Finish repaying it before borrowing again.";
        feedback_until = SDL_GetTicks() + 5500;
    };
    auto cycle_giving = [&] {
        const int next = (static_cast<int>(world.snapshot().giving_policy) + 1) % 3;
        world.set_giving_policy(static_cast<grace::GivingPolicy>(next));
        feedback = std::string("Giving set to ") + giving_name(world.snapshot().giving_policy) + ".";
        feedback_until = SDL_GetTicks() + 3500;
    };
    auto cycle_services = [&] {
        const int next = (static_cast<int>(world.snapshot().service_budget) + 1) % 3;
        world.set_service_budget(static_cast<grace::ServiceBudget>(next));
        feedback = std::string("Services set to ") + service_name(world.snapshot().service_budget) + ".";
        feedback_until = SDL_GetTicks() + 3500;
    };
    auto cycle_road_budget = [&] {
        const int next = (static_cast<int>(world.snapshot().road_budget) + 1) % 3;
        world.set_road_budget(static_cast<grace::RoadBudget>(next));
        feedback = std::string("Road maintenance set to ") +
            road_budget_name(world.snapshot().road_budget) + ".";
        feedback_until = SDL_GetTicks() + 3500;
    };
    auto cycle_tax = [&] {
        const int next = (static_cast<int>(world.snapshot().tax_policy) + 1) % 3;
        world.set_tax_policy(static_cast<grace::TaxPolicy>(next));
        feedback = std::string("Business tax set to ") + tax_name(world.snapshot().tax_policy) +
            ". Higher taxes raise revenue but slow new business growth.";
        feedback_until = SDL_GetTicks() + 4500;
    };
    auto toggle_priority = [&] {
        world.set_care_priority(!world.snapshot().care_first);
        feedback = world.snapshot().care_first
            ? "Volunteers now staff pantries before chapels."
            : "Volunteers now staff chapels before pantries.";
        feedback_until = SDL_GetTicks() + 4000;
    };
    auto start_event = [&](grace::EventKind kind) {
        feedback = event_message(world.host_event(kind), kind,
                                 world.snapshot().event_cooldown);
        feedback_until = SDL_GetTicks() + 4000;
    };
    auto step_week = [&] {
        if (!paused || !simulation_started || world.snapshot().bankrupt) {
            feedback = "Pause a running town to step one week.";
        } else {
            world.advance_week();
            record_finances();
            feedback = world.snapshot().bankrupt
                ? "BANKRUPT: review the budget or start a new town."
                : "Advanced one week.";
            if (world.snapshot().bankrupt) {
                budget_open = false;
                options_open = false;
                painting_tiles = false;
            }
        }
        feedback_until = SDL_GetTicks() + 4000;
    };
    auto save_game = [&] {
        if (save_file.empty()) {
            feedback = "The user save folder is unavailable.";
        } else {
            std::error_code error;
            if (!save_file.parent_path().empty())
                std::filesystem::create_directories(save_file.parent_path(), error);
            auto temporary = save_file;
            temporary += ".tmp";
            bool written = false;
            if (!error) {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (output) {
                    output << "GRACE_VALLEY_GAME 1\n"
                           << static_cast<int>(paused) << ' '
                           << static_cast<int>(simulation_started) << ' '
                           << static_cast<int>(selected) << ' '
                           << static_cast<int>(upgrade_mode) << ' '
                           << std::setprecision(std::numeric_limits<float>::max_digits10)
                           << camera.zoom << ' ' << camera.offset_x << ' '
                           << camera.offset_y << '\n';
                    written = world.save(output);
                    output.flush();
                    written = written && output.good();
                    output.close();
                    written = written && !output.fail();
                }
            }
            if (written && replace_save_file(temporary, save_file))
                feedback = "Town saved to grace-valley.sav (F5).";
            else {
                std::filesystem::remove(temporary, error);
                feedback = "Could not write the save file.";
            }
        }
        feedback_until = SDL_GetTicks() + 5000;
    };
    auto load_game = [&] {
        if (save_file.empty()) {
            feedback = "The user save folder is unavailable.";
        } else {
            std::ifstream input(save_file, std::ios::binary);
            if (!input) feedback = "No save found. Press F5 to save your town.";
            else {
                std::string magic;
                int version = 0, saved_paused = 0, saved_started = 0;
                int saved_tool = 0, saved_upgrade = 0;
                Camera restored_camera;
                bool valid = static_cast<bool>(input >> magic >> version >> saved_paused >>
                    saved_started >> saved_tool >> saved_upgrade >> restored_camera.zoom >>
                    restored_camera.offset_x >> restored_camera.offset_y);
                valid = valid && magic == "GRACE_VALLEY_GAME" && version == 1 &&
                    saved_paused >= 0 && saved_paused <= 1 &&
                    saved_started >= 0 && saved_started <= 1 &&
                    saved_upgrade >= 0 && saved_upgrade <= 1 &&
                    (saved_started || saved_paused) &&
                    std::isfinite(restored_camera.zoom) &&
                    std::isfinite(restored_camera.offset_x) &&
                    std::isfinite(restored_camera.offset_y) &&
                    restored_camera.zoom >= 0.35f && restored_camera.zoom <= 2.25f &&
                    std::abs(restored_camera.offset_x) < 100000.0f &&
                    std::abs(restored_camera.offset_y) < 100000.0f;
                valid = valid && saved_tool >= 0 &&
                    saved_tool <= static_cast<int>(grace::Tile::power_plant);
                const auto tool = static_cast<grace::Tile>(saved_tool);
                valid = valid && std::find(tools.begin(), tools.end(), tool) != tools.end() &&
                    (!saved_upgrade || tool == grace::Tile::road);
                auto restored = valid ? grace::World::load(input) : std::nullopt;
                if (restored) {
                    input >> std::ws;
                    valid = input.peek() == std::char_traits<char>::eof() &&
                        (!restored->snapshot().bankrupt || saved_paused);
                } else valid = false;
                if (!valid) feedback = "Save is damaged or uses an unsupported version.";
                else {
                    world = std::move(*restored);
                    finance_history.clear();
                    record_finances();
                    advisor_open = false;
                    advisor_acknowledged = false;
                    camera = restored_camera;
                    camera.clamp();
                    selected = tool;
                    upgrade_mode = saved_upgrade != 0;
                    hand_mode = false;
                    bulldozer_mode = false;
                    paused = saved_paused != 0;
                    simulation_started = saved_started != 0;
                    budget_open = false;
                    reports_open = false;
                    report_tab = ReportTab::overview;
                    options_open = false;
                    dragging = false;
                    painting_tiles = false;
                    last_week = SDL_GetTicks();
                    car_animation_ms = 0;
                    feedback = "Town loaded from grace-valley.sav (F9).";
                }
            }
        }
        feedback_until = SDL_GetTicks() + 5000;
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
        click(tool_button(1).x + 40, tool_button(1).y + 35);
        click(margin + 9.5f * base_cell, header + 7.5f * base_cell);
        SDL_Event zone_drag{};
        zone_drag.type = SDL_EVENT_MOUSE_MOTION;
        zone_drag.motion.x = margin + 8.5f * base_cell;
        zone_drag.motion.y = header + 7.5f * base_cell;
        SDL_PushEvent(&zone_drag);
        click(tool_button(2).x + 40, tool_button(2).y + 35);
        click(margin + 11.5f * base_cell, header + 9.5f * base_cell);
        click(tool_button(3).x + 40, tool_button(3).y + 35);
        click(margin + 13.5f * base_cell, header + 9.5f * base_cell);
        click(options_button.x + 50, options_button.y + 20);
        click(option_button(1).x + 80, option_button(1).y + 20);
    }
    if (toolbar_smoke) {
        auto click = [&](SDL_FRect rect) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = rect.x + rect.w / 2;
            event.button.y = rect.y + rect.h / 2;
            SDL_PushEvent(&event);
        };
        click(play_button);
        click(options_button);
        click(option_button(4));
        click(option_button(0));
        click({static_cast<float>(sidebar_x + 8), 134, 172, 34});
        click(tool_button(5));
        click(budget_button);
    }
    if (power_smoke) {
        SDL_Event choose{};
        choose.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        choose.button.button = SDL_BUTTON_LEFT;
        choose.button.x = tool_button(7).x + 40;
        choose.button.y = tool_button(7).y + 35;
        SDL_PushEvent(&choose);
        SDL_Event place = choose;
        place.button.x = margin + 13.5f * base_cell;
        place.button.y = header + 9.5f * base_cell;
        SDL_PushEvent(&place);
    }
    if (traffic_smoke) {
        SDL_Event choose{};
        choose.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        choose.button.button = SDL_BUTTON_LEFT;
        choose.button.x = upgrade_button.x + upgrade_button.w / 2;
        choose.button.y = upgrade_button.y + upgrade_button.h / 2;
        SDL_PushEvent(&choose);
        SDL_Event upgrade = choose;
        upgrade.button.x = margin + 12.5f * base_cell;
        upgrade.button.y = header + 8.5f * base_cell;
        SDL_PushEvent(&upgrade);
    }
    if (save_smoke) {
        auto click = [&](Uint8 button, float x, float y) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = button;
            event.button.x = x;
            event.button.y = y;
            SDL_PushEvent(&event);
        };
        click(SDL_BUTTON_LEFT, tool_button(7).x + 40, tool_button(7).y + 35);
        click(SDL_BUTTON_LEFT, margin + 13.5f * base_cell,
              header + 9.5f * base_cell);
        click(SDL_BUTTON_LEFT, save_button.x + 34, save_button.y + 20);
        click(SDL_BUTTON_LEFT, tool_button(0).x + 40, tool_button(0).y + 35);
        click(SDL_BUTTON_LEFT, margin + 16.5f * base_cell,
              header + 8.5f * base_cell);
        click(SDL_BUTTON_LEFT, save_button.x + 34, save_button.y + 20);
        click(SDL_BUTTON_RIGHT, margin + 16.5f * base_cell,
              header + 8.5f * base_cell);
        click(SDL_BUTTON_RIGHT, margin + 13.5f * base_cell,
              header + 9.5f * base_cell);
        click(SDL_BUTTON_LEFT, load_button.x + 34, load_button.y + 20);
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
        SDL_Event choose_bulldozer{};
        choose_bulldozer.type = SDL_EVENT_KEY_DOWN;
        choose_bulldozer.key.key = SDLK_9;
        SDL_PushEvent(&choose_bulldozer);
        click(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 11, 9);
        click(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 11, 9);
        click(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 11, 8);
        click(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 11, 8);
        SDL_Event choose_road{};
        choose_road.type = SDL_EVENT_KEY_DOWN;
        choose_road.key.key = SDLK_1;
        SDL_PushEvent(&choose_road);
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
    if (bridge_smoke) {
        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = margin + 16.5f * base_cell;
        down.button.y = header + 8.5f * base_cell;
        SDL_PushEvent(&down);
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        for (const auto& target : {std::array<int, 2>{17, 8},
                                   std::array<int, 2>{17, 4},
                                   std::array<int, 2>{23, 4}}) {
            motion.motion.x = margin + (target[0] + 0.5f) * base_cell;
            motion.motion.y = header + (target[1] + 0.5f) * base_cell;
            SDL_PushEvent(&motion);
        }
        SDL_Event up{};
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        up.button.button = SDL_BUTTON_LEFT;
        up.button.x = motion.motion.x;
        up.button.y = motion.motion.y;
        SDL_PushEvent(&up);
    }
    if (pan_smoke) {
        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.button = SDL_BUTTON_RIGHT;
        down.button.x = 520;
        down.button.y = 300;
        SDL_PushEvent(&down);
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = 400;
        motion.motion.y = 300;
        motion.motion.xrel = -120;
        SDL_PushEvent(&motion);
        SDL_Event up{};
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        up.button.button = SDL_BUTTON_RIGHT;
        SDL_PushEvent(&up);
        SDL_Event select_hand{};
        select_hand.type = SDL_EVENT_KEY_DOWN;
        select_hand.key.key = SDLK_0;
        SDL_PushEvent(&select_hand);
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == pan_button) {
                dragging = false;
                pan_button = 0;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_LEFT) painting_tiles = false;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                dragging = false;
                pan_button = 0;
                painting_tiles = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_F5) { save_game(); continue; }
                if (event.key.key == SDLK_F9) { load_game(); continue; }
                if (event.key.key == SDLK_ESCAPE) {
                    if (advisor_open) advisor_open = false;
                    else if (budget_open) budget_open = false;
                    else if (reports_open) reports_open = false;
                    else if (options_open) options_open = false;
                    else if (!hand_mode && !world.snapshot().bankrupt) {
                        hand_mode = true;
                        bulldozer_mode = false;
                        upgrade_mode = false;
                        painting_tiles = false;
                    } else running = false;
                    continue;
                }
                if (event.key.key == SDLK_BACKSPACE) {
                    new_town();
                    continue;
                }
                if (event.key.key == SDLK_B) {
                    advisor_open = false;
                    budget_open = !budget_open;
                    if (budget_open) { options_open = false; reports_open = false; }
                    continue;
                }
                if (event.key.key == SDLK_I) {
                    advisor_open = false;
                    reports_open = !reports_open;
                    if (reports_open) { budget_open = false; options_open = false; }
                    continue;
                }
                if (reports_open && event.key.key == SDLK_TAB) {
                    report_tab = report_tab == ReportTab::overview
                        ? ReportTab::power : ReportTab::overview;
                    continue;
                }
                if (world.snapshot().bankrupt) continue;
                if (event.key.key == SDLK_SPACE) {
                    advisor_open = false;
                    toggle_play();
                }
                if (event.key.key == SDLK_U) {
                    upgrade_mode = !upgrade_mode;
                    if (upgrade_mode) {
                        selected = grace::Tile::road;
                        hand_mode = false;
                        bulldozer_mode = false;
                        options_open = false;
                    }
                    painting_tiles = false;
                }
                if (event.key.key == SDLK_N) step_week();
                if (event.key.key == SDLK_G) cycle_giving();
                if (event.key.key == SDLK_O) cycle_services();
                if (event.key.key == SDLK_T) cycle_tax();
                if (event.key.key == SDLK_M) cycle_road_budget();
                if (event.key.key == SDLK_V) toggle_priority();
                if (event.key.key == SDLK_E) start_event(grace::EventKind::gathering);
                if (event.key.key == SDLK_F) start_event(grace::EventKind::food_drive);
                if (event.key.key == SDLK_R) camera = Camera{};
                if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS)
                    camera.zoom_at(1.15f, margin + map_width / 2.0f, header + map_height / 2.0f);
                if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                    camera.zoom_at(1.0f / 1.15f, margin + map_width / 2.0f, header + map_height / 2.0f);
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_8) {
                    selected = tools[static_cast<std::size_t>(event.key.key - SDLK_1)];
                    upgrade_mode = false;
                    bulldozer_mode = false;
                    hand_mode = false;
                    options_open = false;
                    painting_tiles = false;
                }
                if (event.key.key == SDLK_9) {
                    bulldozer_mode = !bulldozer_mode;
                    if (bulldozer_mode) hand_mode = false;
                    upgrade_mode = false;
                    painting_tiles = false;
                    options_open = false;
                    feedback = bulldozer_mode ? "Bulldozer selected. Click or drag to clear roads, zones, and buildings." : "Bulldozer off.";
                    feedback_until = SDL_GetTicks() + 3500;
                }
                if (event.key.key == SDLK_0 || event.key.key == SDLK_H) {
                    hand_mode = true;
                    bulldozer_mode = false;
                    upgrade_mode = false;
                    painting_tiles = false;
                    feedback = "Pan tool selected. Right-drag or use WASD to move the map.";
                    feedback_until = SDL_GetTicks() + 3500;
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
                bulldozer_mode) {
                float mx = event.motion.x, my = event.motion.y;
                SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
                int x, y;
                if (camera.tile_at(mx, my, x, y) && (x != paint_x || y != paint_y)) {
                    if (world.remove(x, y)) feedback = "Bulldozed. Tile cleared.";
                    paint_x = x;
                    paint_y = y;
                    feedback_until = SDL_GetTicks() + 2000;
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && painting_tiles &&
                !bulldozer_mode && (selected == grace::Tile::road || is_zone(selected))) {
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
                                   (selected == grace::Tile::road ?
                                        (result.bridges > 0 ?
                                            " road tiles including bridges built for " :
                                            " road tiles built for ") :
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
                    pan_button = SDL_BUTTON_MIDDLE;
                    continue;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    float pan_x = event.button.x, pan_y = event.button.y;
                    SDL_RenderCoordinatesFromWindow(renderer, pan_x, pan_y, &pan_x, &pan_y);
                    if (!world.snapshot().bankrupt && !budget_open && !reports_open &&
                        !advisor_open && pan_x >= margin && pan_x < margin + map_width &&
                        pan_y >= header && pan_y < header + map_height) {
                        dragging = true;
                        pan_button = SDL_BUTTON_RIGHT;
                        painting_tiles = false;
                    }
                    continue;
                }
                float mx = event.button.x;
                float my = event.button.y;
                SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
                if (advisor_open) {
                    if (event.button.button == SDL_BUTTON_LEFT &&
                        contains(advisor_loan_button, mx, my)) {
                        borrow_money();
                        advisor_open = false;
                    } else if (event.button.button == SDL_BUTTON_LEFT &&
                        contains(advisor_budget_button, mx, my)) {
                        advisor_open = false;
                        budget_open = true;
                    } else if (event.button.button == SDL_BUTTON_LEFT &&
                               contains(advisor_resume_button, mx, my)) {
                        advisor_open = false;
                        toggle_play();
                    }
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    world.snapshot().congested_roads > 0 &&
                    contains(traffic_alert_button, mx, my)) {
                    reports_open = true;
                    report_tab = ReportTab::overview;
                    budget_open = false;
                    options_open = false;
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    contains(save_button, mx, my)) {
                    save_game();
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    contains(load_button, mx, my)) {
                    load_game();
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    contains(play_button, mx, my)) {
                    toggle_play();
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    contains(reports_button, mx, my)) {
                    reports_open = !reports_open;
                    if (reports_open) { budget_open = false; options_open = false; }
                    painting_tiles = false;
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    contains(budget_button, mx, my)) {
                    budget_open = !budget_open;
                    if (budget_open) { options_open = false; reports_open = false; }
                    painting_tiles = false;
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT &&
                    contains(options_button, mx, my)) {
                    options_open = !options_open;
                    if (options_open) { budget_open = false; reports_open = false; }
                    painting_tiles = false;
                    continue;
                }
                if (reports_open && event.button.button == SDL_BUTTON_LEFT &&
                    contains(report_overview_tab, mx, my)) {
                    report_tab = ReportTab::overview;
                    continue;
                }
                if (reports_open && event.button.button == SDL_BUTTON_LEFT &&
                    contains(report_power_tab, mx, my)) {
                    report_tab = ReportTab::power;
                    continue;
                }
                if (reports_open && contains(reports_panel, mx, my)) continue;
                if (budget_open) {
                    if (event.button.button == SDL_BUTTON_LEFT &&
                        !world.snapshot().bankrupt) {
                        if (contains(budget_loan_button, mx, my)) {
                            borrow_money();
                            continue;
                        }
                        for (int row = 0; row < 4; ++row)
                            for (int choice = 0; choice < 3; ++choice)
                                if (contains(budget_choice(row, choice), mx, my)) {
                                    switch (row) {
                                    case 0: world.set_giving_policy(static_cast<grace::GivingPolicy>(choice)); break;
                                    case 1: world.set_service_budget(static_cast<grace::ServiceBudget>(choice)); break;
                                    case 2: world.set_tax_policy(static_cast<grace::TaxPolicy>(choice)); break;
                                    case 3: world.set_road_budget(static_cast<grace::RoadBudget>(choice)); break;
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
                if (mx >= sidebar_x && mx < sidebar_x + sidebar_width &&
                    my >= header && my < header + map_height) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (options_open) {
                            const SDL_FRect back{static_cast<float>(sidebar_x + 8), 134, 172, 34};
                            if (contains(back, mx, my)) options_open = false;
                            for (int row = 0; row < 10; ++row) {
                                if (!contains(option_button(row), mx, my)) continue;
                                switch (row) {
                                case 0: toggle_play(); break;
                                case 1: budget_open = true; options_open = false; break;
                                case 2: cycle_giving(); break;
                                case 3: cycle_services(); break;
                                case 4: cycle_tax(); break;
                                case 5: toggle_priority(); break;
                                case 6: start_event(grace::EventKind::gathering); break;
                                case 7: start_event(grace::EventKind::food_drive); break;
                                case 8: camera = Camera{}; break;
                                case 9: step_week(); break;
                                }
                                break;
                            }
                        } else {
                            if (contains(hand_button, mx, my)) {
                                hand_mode = true;
                                bulldozer_mode = false;
                                upgrade_mode = false;
                                painting_tiles = false;
                                feedback = "Pan tool selected. Right-drag or use WASD to move the map.";
                                feedback_until = SDL_GetTicks() + 3500;
                                continue;
                            }
                            if (contains(bulldozer_button, mx, my)) {
                                bulldozer_mode = !bulldozer_mode;
                                if (bulldozer_mode) hand_mode = false;
                                upgrade_mode = false;
                                painting_tiles = false;
                                feedback = bulldozer_mode ? "Bulldozer selected. Click or drag to clear roads, zones, and buildings." : "Bulldozer off.";
                                feedback_until = SDL_GetTicks() + 3500;
                                continue;
                            }
                            if (contains(upgrade_button, mx, my)) {
                                upgrade_mode = !upgrade_mode;
                                if (upgrade_mode) {
                                    selected = grace::Tile::road;
                                    bulldozer_mode = false;
                                    hand_mode = false;
                                }
                                painting_tiles = false;
                                feedback = upgrade_mode
                                    ? "Road upgrade: click a road tile to advance one level."
                                    : "Road upgrade mode off.";
                                feedback_until = SDL_GetTicks() + 4000;
                            }
                            for (std::size_t index = 0; index < tools.size(); ++index)
                                if (contains(tool_button(static_cast<int>(index)), mx, my)) {
                                    selected = tools[index];
                                    upgrade_mode = false;
                                    bulldozer_mode = false;
                                    hand_mode = false;
                                    painting_tiles = false;
                                    feedback = std::string(grace::World::name(selected)) +
                                        " selected. Cost " +
                                        std::to_string(grace::World::cost(selected)) + " funds.";
                                    feedback_until = SDL_GetTicks() + 3000;
                                    break;
                                }
                        }
                    }
                    continue;
                }
                int x, y;
                if (camera.tile_at(mx, my, x, y)) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (hand_mode) continue;
                        if (bulldozer_mode) {
                            painting_tiles = true;
                            paint_x = x;
                            paint_y = y;
                            if (world.remove(x, y)) feedback = "Bulldozed. Tile cleared.";
                            else feedback = (x == 12 && y == 8)
                                ? "The chapel's starting road cannot be removed."
                                : "There is nothing to demolish here.";
                            feedback_until = SDL_GetTicks() + 3000;
                            continue;
                        }
                        if (upgrade_mode || (SDL_GetModState() & SDL_KMOD_SHIFT)) {
                            painting_tiles = false;
                            const int price = world.next_road_upgrade_cost(x, y);
                            if (world.upgrade_road(x, y))
                                feedback = std::string("Road upgraded to ") +
                                    grace::World::road_level_name(world.road_level(x, y)) +
                                    ": capacity " + std::to_string(world.road_capacity(x, y)) +
                                    ", cost " + std::to_string(price) + " funds.";
                            else if (world.tile(x, y) != grace::Tile::road)
                                feedback = "Choose an existing road tile to upgrade.";
                            else if (price == 0)
                                feedback = "This road is already at its highest level.";
                            else feedback = "Next road upgrade needs " +
                                std::to_string(price) + " funds.";
                            feedback_until = SDL_GetTicks() + 4000;
                            continue;
                        }
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
                            const bool bridge = selected == grace::Tile::road &&
                                world.terrain(x, y) == grace::Terrain::water;
                            feedback = std::string(bridge ? "Bridge" : grace::World::name(selected)) +
                                (zoning ? " marked for " : " built for ") +
                                std::to_string(price) + " funds.";
                        } else feedback = placement_message(error);
                    }
                    feedback_until = SDL_GetTicks() + 3500;
                }
            }
        }

        const Uint64 now = SDL_GetTicks();
        const Uint64 frame_ms = now - last_frame;
        const float dt = std::min(0.05f, static_cast<float>(frame_ms) / 1000.0f);
        last_frame = now;
        if (!paused) car_animation_ms += frame_ms;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const float movement = 300.0f * dt;
        camera.pan((keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? movement : 0.0f) -
                       (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? movement : 0.0f),
                   (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? movement : 0.0f) -
                       (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? movement : 0.0f));
        if (paused) last_week = now;
        if (!paused && now - last_week >= 2500) {
            world.advance_week();
            record_finances();
            last_week = now;
            if (world.snapshot().bankrupt) {
                paused = true;
                budget_open = false;
                dragging = false;
                pan_button = 0;
                painting_tiles = false;
                feedback = "BANKRUPT: weekly expenses emptied the treasury. Press Backspace for a new town.";
                feedback_until = now + 10000;
            }
        }
        if (soak_seconds > 0 && now - last_autoplay >= 2500) {
            autoplay_step(world);
            record_finances();
            last_autoplay = now;
        }
        const auto advisor_state = world.snapshot();
        const int weekly_base_net = advisor_state.weekly_income -
            advisor_state.weekly_upkeep + advisor_state.loan_due_this_week;
        int projected_funds = advisor_state.treasury;
        int warning_weeks = 0;
        for (int offset = 0; offset < 5; ++offset) {
            projected_funds += weekly_base_net;
            if ((advisor_state.week + offset) % 4 == 0)
                projected_funds -= advisor_state.monthly_loan_payment;
            if (projected_funds < 0) {
                warning_weeks = offset + 1;
                break;
            }
        }
        if (advisor_state.treasury >= 0 && !advisor_state.bankrupt &&
            simulation_started && !paused && warning_weeks > 0 && warning_weeks <= 3 &&
            !advisor_acknowledged) {
            advisor_open = true;
            advisor_acknowledged = true;
            paused = true;
            budget_open = false;
            reports_open = false;
            options_open = false;
            feedback = "Your city advisor sees a budget crisis approaching.";
            feedback_until = now + 5000;
        }
        if (warning_weeks == 0) advisor_acknowledged = false;
        title(window, world.snapshot(), selected, paused, simulation_started);
        SDL_SetRenderDrawColor(renderer, 34, 51, 55, 255);
        SDL_RenderClear(renderer);

        const auto state = world.snapshot();
        fill(renderer, {20, 10, 292, 62}, {43, 65, 65});
        fill(renderer, {20, 10, 3, 62}, {202, 169, 104});
        fill(renderer, {330, 10, 270, 62}, {43, 65, 65});
        fill(renderer, {330, 10, 3, 62}, {105, 184, 114});
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        SDL_RenderDebugTextFormat(renderer, 34, 14, "WEEK %d  |  %s",
                                  state.week, state.bankrupt ? "BANKRUPT" :
                                  !simulation_started ? "SETUP" : paused ? "PAUSED" : "RUNNING");
        SDL_RenderDebugTextFormat(renderer, 34, 29,
            "PEOPLE %d / %d  |  JOY %d", state.population,
            state.capacity, state.happiness);
        SDL_RenderDebugTextFormat(renderer, 34, 44,
            "FUNDS %d  |  IN %d  OUT %d", state.treasury,
            state.weekly_income, state.weekly_upkeep);
        SDL_RenderDebugTextFormat(renderer, 34, 59,
            "NET %+d PER WEEK", state.weekly_income - state.weekly_upkeep);
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        SDL_RenderDebugTextFormat(renderer, 344, 14,
            "CITY SCORE  %d / 100", state.city_score);
        fill(renderer, {344, 38, 238, 10}, {66, 82, 83});
        const Color score_color = state.city_score >= 70 ? Color{102, 188, 121} :
            state.city_score >= 45 ? Color{232, 181, 86} : Color{215, 92, 76};
        fill(renderer, {344, 38, 2.38f * state.city_score, 10}, score_color);
        SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
        SDL_RenderDebugText(renderer, 344, 56, "COMMUNITY HEALTH & GROWTH");
        draw_button(renderer, play_button, paused ? "PLAY" : "PAUSE", !paused);
        draw_button(renderer, budget_button, "BUDGET", budget_open);
        draw_button(renderer, reports_button, "REPORTS", reports_open);
        draw_button(renderer, options_button, "OPTIONS", options_open);
        draw_button(renderer, save_button, "SAVE");
        draw_button(renderer, load_button, "LOAD");
        if (logo) {
            const SDL_FRect logo_rect{static_cast<float>(window_width - 82), 3, 64, 64};
            SDL_RenderTexture(renderer, logo, nullptr, &logo_rect);
        }
        SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
        const std::array<int, 3> demand_values{state.residential_demand,
            state.commercial_demand, state.industrial_demand};
        const std::array<const char*, 3> demand_initials{"R", "C", "I"};
        const std::array<Color, 3> demand_colors{{
            {105, 184, 114}, {91, 173, 187}, {218, 174, 103}}};
        for (int index = 0; index < 3; ++index) {
            const auto item = demand_item(index);
            const auto color = demand_colors[static_cast<std::size_t>(index)];
            fill(renderer, {item.x, item.y + 1, item.w, 21}, {43, 65, 65});
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            SDL_RenderDebugText(renderer, item.x + 4, item.y + 7,
                                demand_initials[static_cast<std::size_t>(index)]);
            fill(renderer, {item.x + 20, item.y + 9, 48, 6}, {66, 82, 83});
            fill(renderer, {item.x + 20, item.y + 9,
                0.48f * demand_values[static_cast<std::size_t>(index)], 6}, color);
            SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
            SDL_RenderDebugTextFormat(renderer, item.x + 71, item.y + 7,
                "%02d", demand_values[static_cast<std::size_t>(index)]);
        }
        SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
        SDL_RenderDebugText(renderer, 318, 76,
            state.bankrupt
                ? "BANKRUPT  |  NEW TOWN OR VIEW BUDGET  |  BACKSPACE RESTARTS"
                : "1-9 TOOLS  |  RIGHT DRAG / WASD PAN  |  I REPORTS");
        if (!state.bankrupt) {
            SDL_SetRenderDrawColor(renderer, 249, 176, 116, 255);
            if (state.congested_roads > 0)
                SDL_RenderDebugTextFormat(renderer, 804, 76,
                    "TRAFFIC %d | REPORTS", state.congested_roads);
            else if (state.noise_affected_homes > 0)
                SDL_RenderDebugTextFormat(renderer, 804, 76,
                    "NOISE %d HOMES", state.noise_affected_homes);
            else if (state.bad_roads > 0)
                SDL_RenderDebugTextFormat(renderer, 804, 76,
                    "BAD ROADS %d", state.bad_roads);
            else if (state.abandoned_buildings > 0)
                SDL_RenderDebugTextFormat(renderer, 804, 76,
                    "ABANDONED %d", state.abandoned_buildings);
            else if (state.city_score < 30)
                SDL_RenderDebugText(renderer, 804, 76, "CITY SCORE CRITICAL");
        }        const SDL_Rect map_clip{margin, header, map_width, map_height};
        SDL_SetRenderClipRect(renderer, &map_clip);
        for (int y = 0; y < grace::World::height; ++y)
            for (int x = 0; x < grace::World::width; ++x)
                draw_tile(renderer, world, x, y, camera, car_animation_ms);

        // Workshops and connected power plants emit drifting smoke.
        // The effect is visual only; pollution's gameplay impact remains in World.
        for (int y = 0; y < grace::World::height; ++y) {
            for (int x = 0; x < grace::World::width; ++x) {
                const auto source = world.tile(x, y);
                if (source != grace::Tile::industrial &&
                    source != grace::Tile::abandoned_industrial &&
                    !(source == grace::Tile::power_plant && world.is_powered(x, y))) continue;
                const auto building = camera.tile_rect(x, y);
                const float phase = static_cast<float>(now) * 0.000035f +
                    static_cast<float>((x * 17 + y * 31) % 97) * 0.09f;
                for (int puff = 0; puff < 4; ++puff) {
                    const float travel = std::fmod(phase + puff * 0.31f, 1.0f);
                    const float size = camera.cell_size() * (0.23f + 0.08f * puff);
                    const float px = building.x + building.w * 0.53f +
                        travel * camera.cell_size() * 1.7f - size * 0.4f;
                    const float py = building.y + building.h * 0.04f -
                        travel * camera.cell_size() * 1.25f - puff * 3.0f;
                    tint(renderer, {px, py, size, size * 0.56f}, {50, 55, 55},
                         static_cast<Uint8>(105 - puff * 13));
                    tint(renderer, {px + size * 0.22f, py - size * 0.18f,
                                    size * 0.58f, size * 0.45f}, {67, 70, 68},
                         static_cast<Uint8>(86 - puff * 12));
                }
            }
        }

        float mx, my;
        if (capture) {
            mx = margin + (capture_bridge ? 20.5f : capture_traffic ? 12.5f :
                capture_roads || capture_zones ? 10.5f : 11.5f) * base_cell;
            my = header + (capture_bridge ? 5.5f : capture_traffic ? 8.5f : 9.5f) * base_cell;
        } else {
            SDL_GetMouseState(&mx, &my);
            SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);
        }
        int hovered_demand = -1;
        if (!capture && !advisor_open && !state.bankrupt) {
            for (int index = 0; index < 3; ++index)
                if (contains(demand_item(index), mx, my)) {
                    hovered_demand = index;
                    break;
                }
        }
        if (hovered_demand != demand_hover) {
            demand_hover = hovered_demand;
            demand_hover_started = now;
        }
        int hover_x = -1, hover_y = -1;
        const bool over_tile = camera.tile_at(mx, my, hover_x, hover_y);
        const bool tooltip_area = over_tile && !budget_open && !reports_open && !options_open &&
                                  !state.bankrupt && !dragging && !painting_tiles;
        if (!tooltip_area) {
            hover_tracking = false;
        } else if (!hover_tracking || hover_x != hover_track_x ||
                   hover_y != hover_track_y || std::abs(mx - hover_anchor_x) > 4.0f ||
                   std::abs(my - hover_anchor_y) > 4.0f) {
            hover_tracking = true;
            hover_track_x = hover_x;
            hover_track_y = hover_y;
            hover_anchor_x = mx;
            hover_anchor_y = my;
            hover_started = now;
        }
        const bool stroke_start = over_tile && !hand_mode && !upgrade_mode &&
            !bulldozer_mode && (selected == grace::Tile::road || is_zone(selected)) &&
            world.tile(hover_x, hover_y) == selected;
        const bool service_view = !hand_mode &&
            (selected == grace::Tile::chapel || selected == grace::Tile::pantry);
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
        if (!hand_mode && selected == grace::Tile::road && !upgrade_mode && over_tile &&
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
                    draw_tile(renderer, road_preview, x, y, camera, car_animation_ms);
            }
            auto ghost = camera.tile_rect(hover_x, hover_y);
            tint(renderer, ghost, {252, 210, 110}, 65);
        }
        if (over_tile && !hand_mode) {
            const bool valid = bulldozer_mode
                ? world.tile(hover_x, hover_y) != grace::Tile::meadow
                : upgrade_mode
                ? world.tile(hover_x, hover_y) == grace::Tile::road &&
                  world.next_road_upgrade_cost(hover_x, hover_y) > 0 &&
                  state.treasury >= world.next_road_upgrade_cost(hover_x, hover_y)
                : world.can_place(hover_x, hover_y, selected) || stroke_start;
            const Color preview = valid
                ? Color{253, 229, 153} : Color{225, 91, 83};
                SDL_SetRenderDrawColor(renderer, preview.r, preview.g, preview.b, 255);
                auto rect = camera.tile_rect(hover_x, hover_y);
                rect.x += 2; rect.y += 2; rect.w -= 4; rect.h -= 4;
            if (bulldozer_mode) {
                tint(renderer, rect, {235, 112, 88}, 45);
                SDL_SetRenderDrawColor(renderer, 235, 112, 88, 255);
                SDL_RenderRect(renderer, &rect);
            } else SDL_RenderRect(renderer, &rect);
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        if (tooltip_area && now - hover_started >= 1200) {
            const MapTooltip tip = map_tooltip(world, hover_x, hover_y);
            std::size_t longest = tip.title.size();
            for (int line = 0; line < tip.detail_count; ++line)
                longest = std::max(longest, tip.details[line].size());
            const float width = static_cast<float>(std::min<std::size_t>(
                longest * 8 + 20, static_cast<std::size_t>(map_width - 20)));
            const float height = static_cast<float>(18 + (tip.detail_count + 1) * 16);
            float tooltip_x = mx + 18;
            float tooltip_y = my + 18;
            if (tooltip_x + width > margin + map_width - 6)
                tooltip_x = mx - width - 18;
            if (tooltip_y + height > header + map_height - 6)
                tooltip_y = my - height - 18;
            tooltip_x = std::clamp(tooltip_x, static_cast<float>(margin + 6),
                                   static_cast<float>(margin + map_width - 6) - width);
            tooltip_y = std::clamp(tooltip_y, static_cast<float>(header + 6),
                                   static_cast<float>(header + map_height - 6) - height);
            fill(renderer, {tooltip_x + 3, tooltip_y + 4, width, height}, {14, 23, 23});
            fill(renderer, {tooltip_x, tooltip_y, width, height}, {31, 48, 47});
            SDL_SetRenderDrawColor(renderer, 202, 169, 104, 255);
            const SDL_FRect tooltip_outline{tooltip_x, tooltip_y, width, height};
            SDL_RenderRect(renderer, &tooltip_outline);
            fill(renderer, {tooltip_x + 1, tooltip_y + 1, width - 2, 17}, {91, 75, 48});
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugText(renderer, static_cast<int>(tooltip_x + 9),
                static_cast<int>(tooltip_y + 5), tip.title.c_str());
            for (int line = 0; line < tip.detail_count; ++line)
                SDL_RenderDebugText(renderer, static_cast<int>(tooltip_x + 9),
                    static_cast<int>(tooltip_y + 22 + line * 16),
                    tip.details[line].c_str());
        }
        fill(renderer, {static_cast<float>(sidebar_x), static_cast<float>(header),
                        sidebar_width, map_height}, {29, 45, 46});
        fill(renderer, {static_cast<float>(sidebar_x), static_cast<float>(header),
                        sidebar_width, 4}, {202, 169, 104});
        SDL_SetRenderDrawColor(renderer, 247, 239, 213, 255);
        if (options_open) {
            SDL_RenderDebugText(renderer, sidebar_x + 12, 112, "OPTIONS MENU");
            const SDL_FRect back{static_cast<float>(sidebar_x + 8), 134, 172, 34};
            draw_button(renderer, back, "BACK TO TOOLS");
            const std::array<std::string, 10> labels{
                paused ? "PLAY TOWN" : "PAUSE TOWN", "OPEN BUDGET",
                std::string("GIVING: ") + giving_name(state.giving_policy),
                std::string("SERVICES: ") + service_name(state.service_budget),
                std::string("TAX: ") + tax_name(state.tax_policy),
                std::string("VOLUNTEERS: ") + (state.care_first ? "CARE" : "CHAPEL"),
                "GATHERING (30)", "FOOD DRIVE (25)", "RESET CAMERA",
                "STEP WEEK"};
            for (int row = 0; row < static_cast<int>(labels.size()); ++row)
                draw_button(renderer, option_button(row), labels[static_cast<std::size_t>(row)].c_str());
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, sidebar_x + 12, 688, "ESC CLOSES MENU");
            SDL_RenderDebugText(renderer, sidebar_x + 12, 708, "KEYS STILL WORK");
        } else {
            SDL_RenderDebugText(renderer, sidebar_x + 12, 112, "BUILD TOOLS");
            for (std::size_t index = 0; index < tools.size(); ++index) {
                const auto tool = tools[index];
                const auto rect = tool_button(static_cast<int>(index));
                fill(renderer, rect, !hand_mode && !bulldozer_mode && !upgrade_mode && tool == selected
                    ? Color{202, 169, 104} : Color{69, 92, 91});
                fill(renderer, {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4},
                     !hand_mode && !bulldozer_mode && !upgrade_mode && tool == selected
                         ? Color{93, 91, 66} : Color{43, 65, 65});
                draw_tool_icon(renderer, tool, rect.x + 24, rect.y + 2,
                               static_cast<char>('1' + index));
                SDL_SetRenderDrawColor(renderer, 247, 239, 213, 255);
                SDL_RenderDebugText(renderer, rect.x + 8, rect.y + 42, tool_label(tool));
                SDL_RenderDebugTextFormat(renderer, rect.x + 8, rect.y + 59,
                                          "%dF", grace::World::cost(tool));
            }
            draw_button(renderer, hand_button, "PAN 0", hand_mode);
            draw_button(renderer, bulldozer_button, "", bulldozer_mode);
            fill(renderer, {static_cast<float>(sidebar_x + 101), 471, 21, 20},
                 {195, 132, 75});
            fill(renderer, {static_cast<float>(sidebar_x + 105), 466, 13, 8},
                 {221, 177, 93});
            fill(renderer, {static_cast<float>(sidebar_x + 104), 487, 17, 5},
                 {40, 45, 43});
            fill(renderer, {static_cast<float>(sidebar_x + 97), 493, 7, 7},
                 {40, 45, 43});
            fill(renderer, {static_cast<float>(sidebar_x + 120), 493, 7, 7},
                 {40, 45, 43});
            SDL_SetRenderDrawColor(renderer, 247, 239, 213, 255);
            SDL_RenderDebugText(renderer, sidebar_x + 154, 479, "9");
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, sidebar_x + 12, 507, "ACTIVE TOOL");
            SDL_RenderDebugText(renderer, sidebar_x + 12, 525,
                                hand_mode ? "Pan / inspect" : bulldozer_mode ? "Bulldozer" : upgrade_mode
                                    ? "Road upgrade" : grace::World::name(selected));
            fill(renderer, {static_cast<float>(sidebar_x + 8), 536, 172, 76},
                 {43, 65, 65});
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, sidebar_x + 16, 543, "CITY STATUS");
            const int active_issues = (state.congested_roads > 0) +
                (state.bad_roads > 0) + (state.noise_affected_homes > 0) +
                (state.abandoned_buildings > 0) + (state.city_score < 30);
            SDL_SetRenderDrawColor(renderer, active_issues == 0 ? 142 : 245,
                active_issues == 0 ? 207 : 190, active_issues == 0 ? 151 : 132, 255);
            if (active_issues == 0)
                SDL_RenderDebugText(renderer, sidebar_x + 16, 562, "ALL CLEAR");
            else SDL_RenderDebugTextFormat(renderer, sidebar_x + 16, 562,
                "%d ACTIVE ISSUES", active_issues);
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, sidebar_x + 16, 581, "CLICK REPORTS  |  I");
            draw_button(renderer, upgrade_button, "UPGRADE ROAD", upgrade_mode);
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, sidebar_x + 12, 671,
                                "DRAG ROAD / BRIDGE");
            SDL_RenderDebugText(renderer, sidebar_x + 12, 691,
                                "1-9 SHORTCUTS");
            SDL_RenderDebugText(renderer, sidebar_x + 12, 711,
                                "OPTIONS ABOVE");
        }
        if (budget_open) draw_budget_panel(renderer, state);
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
        fill(renderer, {0, static_cast<float>(header + map_height), window_width,
                        static_cast<float>(footer_height)}, {34, 51, 55});
        const int footer_top = header + map_height;
        SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
        std::string hint = reports_open ? "City reports are open. Click REPORTS or press I to close." :
            now < feedback_until ? feedback : service_view && !over_tile
            ? std::string(grace::World::name(selected)) + " covers " +
                std::to_string(world.homes_with_service(selected)) + "/" +
                std::to_string(state.connected_homes) + " connected homes. Hover to preview coverage."
            : (over_tile ? placement_message(world.placement_error(hover_x, hover_y, selected)) :
                           town_advice(world));
        if (now >= feedback_until && over_tile && is_zone(selected) &&
            world.can_place(hover_x, hover_y, selected)) {
            const int demand = selected == grace::Tile::residential_zone
                ? state.residential_demand : selected == grace::Tile::commercial_zone
                    ? state.commercial_demand : state.industrial_demand;
            hint = std::string(grace::World::name(selected)) + ": costs " +
                   std::to_string(world.cost_at(hover_x, hover_y, selected)) +
                   ", demand " + std::to_string(demand) +
                   "/100. Needs a connected road, power, and 25 demand to develop.";
        }
        if (now >= feedback_until && over_tile && selected == grace::Tile::road &&
            world.terrain(hover_x, hover_y) == grace::Terrain::water &&
            world.tile(hover_x, hover_y) == grace::Tile::meadow)
            hint = world.can_place(hover_x, hover_y, grace::Tile::road)
                ? "Bridge: 25 funds, +1 upkeep. Drag from a road across water."
                : placement_message(world.placement_error(hover_x, hover_y,
                                                          grace::Tile::road));
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
            if (world.is_connected(hover_x, hover_y) &&
                !world.is_powered(hover_x, hover_y)) hint += " No power!";
            if (world.is_noise_polluted(hover_x, hover_y))
                hint += " Noise and pollution from nearby workshops or plants lower wellbeing.";
        }
        if (now >= feedback_until && over_tile) {
            const auto hovered = world.tile(hover_x, hover_y);
            if (hovered == grace::Tile::abandoned_home ||
                hovered == grace::Tile::abandoned_commercial ||
                hovered == grace::Tile::abandoned_industrial)
                hint = std::string(grace::World::name(hovered)) +
                    ": no residents or jobs. Raise City Score above 45 for three weeks to reopen.";
            if (hovered == grace::Tile::road) {
                const int road_level = world.road_level(hover_x, hover_y);
                const int next_cost = world.next_road_upgrade_cost(hover_x, hover_y);
                hint = std::string(world.terrain(hover_x, hover_y) ==
                    grace::Terrain::water ? "Bridge: " : "") +
                    grace::World::road_level_name(road_level) +
                    " traffic " + std::to_string(world.traffic_at(hover_x, hover_y)) +
                    "/" + std::to_string(world.road_capacity(hover_x, hover_y)) +
                    " condition " + std::to_string(world.road_condition(hover_x, hover_y)) + "%" +
                    (next_cost == 0 ? ". Highest level." :
                        ". Press U, then click to upgrade for " +
                        std::to_string(next_cost) + " funds.");
            }
            if (hovered == grace::Tile::residential_zone ||
                hovered == grace::Tile::commercial_zone ||
                hovered == grace::Tile::industrial_zone) {
                const int demand = hovered == grace::Tile::residential_zone
                    ? state.residential_demand : hovered == grace::Tile::commercial_zone
                        ? state.commercial_demand : state.industrial_demand;
                hint = std::string(grace::World::name(hovered)) +
                    (world.is_connected(hover_x, hover_y)
                        ? !world.is_powered(hover_x, hover_y) ?
                            ": no power. Add a connected plant with spare output." :
                          demand >= 25 ? ": eligible for weekly development." :
                            ": waiting for demand to reach 25."
                        : ": waiting for a connected road.");
            } else if (hovered == grace::Tile::commercial ||
                       hovered == grace::Tile::industrial) {
                hint = std::string(grace::World::name(hovered)) +
                    (world.is_connected(hover_x, hover_y)
                        ? world.is_powered(hover_x, hover_y) ?
                            ": provides jobs and business tax revenue." :
                            ": no power. Add a connected plant."
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
        if (now >= feedback_until && !over_tile && !options_open) {
            for (std::size_t index = 0; index < tools.size(); ++index)
                if (contains(tool_button(static_cast<int>(index)), mx, my))
                    hint = std::string(grace::World::name(tools[index])) +
                        ": costs " + std::to_string(grace::World::cost(tools[index])) +
                        " funds. Click to select.";
        }
        if (now >= feedback_until && contains({344, 10, 238, 48}, mx, my))
            hint = "City Score reflects joy, parks, services, road access, power, traffic, and cashflow.";
        if (now >= feedback_until && hand_mode)
            hint = "Pan tool: right-drag or use WASD. Click a build tool to place tiles.";
        if (now >= feedback_until && bulldozer_mode)
            hint = "Bulldozer: click or drag with the left mouse button to remove roads, zones, and buildings.";
        if (now >= feedback_until && demand_hover >= 0 &&
            now - demand_hover_started >= 700) {
            constexpr std::array<const char*, 3> demand_tips{
                "Residential demand: parks, services, power, and quality of life attract households.",
                "Commercial demand follows population and access; powered shops create jobs and tax income.",
                "Industrial demand creates jobs and tax income; workshops add traffic, pollution, and noise."};
            hint = demand_tips[static_cast<std::size_t>(demand_hover)];
        }
        SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
        SDL_RenderDebugTextFormat(renderer, margin, footer_top + 11,
            "ACTIVE TOOL: %s%s", hand_mode ? "PAN / INSPECT" :
                bulldozer_mode ? "BULLDOZER" : upgrade_mode ? "ROAD UPGRADE" :
                grace::World::name(selected), options_open ? "  |  OPTIONS" : "");
        SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
        SDL_RenderDebugText(renderer, margin, footer_top + 31, hint.c_str());
        if (reports_open) {
            tint(renderer, {static_cast<float>(margin), static_cast<float>(header),
                static_cast<float>(map_width), static_cast<float>(map_height)},
                {12, 20, 22}, 170);
            fill(renderer, reports_panel, {29, 45, 46});
            SDL_SetRenderDrawColor(renderer, 202, 169, 104, 255);
            SDL_RenderRect(renderer, &reports_panel);
            fill(renderer, {reports_panel.x + 1, reports_panel.y + 1,
                reports_panel.w - 2, 38}, {43, 65, 65});
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugText(renderer, reports_panel.x + 18,
                reports_panel.y + 14, "CITY REPORTS");
            draw_button(renderer, report_overview_tab, "OVERVIEW",
                        report_tab == ReportTab::overview);
            draw_button(renderer, report_power_tab, "POWER",
                        report_tab == ReportTab::power);
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, reports_panel.x + 620,
                reports_panel.y + 14, "TAB SWITCH  |  ESC CLOSE");

            if (report_tab == ReportTab::power) {
                draw_power_report(renderer, world);
            } else {

            const auto card = [&](SDL_FRect rect, Color accent) {
                fill(renderer, rect, {43, 65, 65});
                fill(renderer, {rect.x, rect.y, 3, rect.h}, accent);
            };
            const int issue_count = (state.congested_roads > 0) +
                (state.bad_roads > 0) + (state.noise_affected_homes > 0) +
                (state.abandoned_buildings > 0) + (state.city_score < 30);
            const auto traffic = traffic_tip(world);
            card({95, 162, 230, 95}, score_color);
            card({335, 162, 590, 95}, issue_count == 0
                ? Color{105, 184, 114} : Color{218, 174, 103});
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, 109, 173, "CITY SCORE");
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugTextFormat(renderer, 109, 192, "%d / 100", state.city_score);
            fill(renderer, {109, 216, 195, 12}, {66, 82, 83});
            fill(renderer, {109, 216, 1.95f * state.city_score, 12}, score_color);
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, 350, 173, "CURRENT ISSUES");
            SDL_SetRenderDrawColor(renderer, issue_count == 0 ? 142 : 255,
                issue_count == 0 ? 207 : 220, issue_count == 0 ? 151 : 166, 255);
            if (issue_count == 0)
                SDL_RenderDebugText(renderer, 350, 195, "All clear. Community health is stable.");
            else {
                if (traffic)
                    SDL_RenderDebugTextFormat(renderer, 350, 194,
                        "Hotspot (%d,%d)  traffic %d/%d  |  %d jammed roads",
                        traffic->x + 1, traffic->y + 1, traffic->flow,
                        traffic->capacity, state.congested_roads);
                else SDL_RenderDebugTextFormat(renderer, 350, 194,
                    "Traffic %d  |  Worn roads %d  |  Noise homes %d",
                    state.congested_roads, state.bad_roads, state.noise_affected_homes);
                SDL_RenderDebugTextFormat(renderer, 350, 216,
                    "Worn %d  |  Noise homes %d  |  Abandoned %d  |  Low score %s",
                    state.bad_roads, state.noise_affected_homes,
                    state.abandoned_buildings, state.city_score < 30 ? "YES" : "NO");
                if (traffic)
                    SDL_RenderDebugTextFormat(renderer, 350, 238,
                        "Try: %s", traffic->action);
            }

            fill(renderer, {95, 272, 830, 133}, {36, 55, 55});
            SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
            SDL_RenderDebugText(renderer, 109, 282, "ZONE DEMAND");
            const auto report_demand = [&](int y, const char* label, int value, Color color) {
                SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
                SDL_RenderDebugText(renderer, 109, y, label);
                fill(renderer, {258, static_cast<float>(y), 450, 10}, {66, 82, 83});
                fill(renderer, {258, static_cast<float>(y), 4.5f * value, 10}, color);
                SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
                SDL_RenderDebugTextFormat(renderer, 725, y, "%d / 100", value);
            };
            report_demand(302, "RESIDENTIAL", state.residential_demand,
                          {105, 184, 114});
            report_demand(333, "COMMERCIAL", state.commercial_demand,
                          {91, 173, 187});
            report_demand(364, "INDUSTRIAL", state.industrial_demand,
                          {218, 174, 103});

            card({95, 420, 260, 155}, {105, 184, 114});
            card({375, 420, 260, 155}, {91, 173, 187});
            card({655, 420, 270, 155}, {218, 174, 103});
            SDL_SetRenderDrawColor(renderer, 245, 238, 211, 255);
            SDL_RenderDebugText(renderer, 109, 431, "TOWN WELLBEING");
            SDL_RenderDebugTextFormat(renderer, 109, 453, "Residents %d / %d",
                                      state.population, state.capacity);
            SDL_RenderDebugTextFormat(renderer, 109, 472, "Joy %d  |  Care %d",
                                      state.happiness, state.care);
            SDL_RenderDebugTextFormat(renderer, 109, 491, "Belonging %d",
                                      state.belonging);
            SDL_RenderDebugTextFormat(renderer, 109, 510, "Homes served %d",
                                      state.served_homes);
            SDL_RenderDebugText(renderer, 389, 431, "ROAD NETWORK");
            SDL_RenderDebugTextFormat(renderer, 389, 453, "Connected roads %d",
                                      state.connected_roads);
            SDL_RenderDebugTextFormat(renderer, 389, 472, "Commuters %d  |  Peak %d",
                                      state.commuters, state.peak_traffic);
            SDL_RenderDebugTextFormat(renderer, 389, 491, "Congested %d  |  Worn %d",
                                      state.congested_roads, state.bad_roads);
            SDL_RenderDebugTextFormat(renderer, 389, 510, "Road condition %d%%",
                                      state.average_road_condition);
            SDL_RenderDebugText(renderer, 669, 431, "COMMUNITY SERVICES");
            SDL_RenderDebugTextFormat(renderer, 669, 453, "Power %d/%d kW Need %d kW",
                state.powered_demand, state.power_capacity, state.power_demand);
            SDL_RenderDebugTextFormat(renderer, 669, 472, "Outages %d  Noise homes %d",
                state.unpowered_buildings, state.noise_affected_homes);
            SDL_RenderDebugTextFormat(renderer, 669, 491, "Chapel %d  Pantry %d",
                state.active_chapels, state.active_pantries);
            SDL_RenderDebugTextFormat(renderer, 669, 510, "Volunteers %d/%d",
                state.volunteers_used, state.volunteers_available);
            SDL_RenderDebugTextFormat(renderer, 669, 529, "Abandoned %d",
                state.abandoned_buildings);

            fill(renderer, {95, 590, 830, 116}, {36, 55, 55});
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, 109, 598, "12-WEEK FINANCIAL TRENDS");
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugTextFormat(renderer, 109, 623,
                "FUNDS %d  |  WEEKLY NET %+d",
                state.treasury,
                state.weekly_income - state.weekly_upkeep);
            SDL_RenderDebugTextFormat(renderer, 540, 623,
                "IN %d  |  OUT %d",
                state.weekly_income, state.weekly_upkeep,
                state.weekly_upkeep);

            const SDL_FRect funds_chart{110, 637, 360, 48};
            const SDL_FRect cashflow_chart{540, 637, 360, 48};
            const auto chart_frame = [&](SDL_FRect area) {
                fill(renderer, area, {31, 48, 48});
                SDL_SetRenderDrawColor(renderer, 76, 94, 91, 255);
                SDL_RenderLine(renderer, area.x, area.y + area.h,
                               area.x + area.w, area.y + area.h);
                for (int row = 1; row <= 2; ++row) {
                    const float gy = area.y + area.h * row / 3.0f;
                    SDL_RenderLine(renderer, area.x, gy, area.x + area.w, gy);
                }
            };
            chart_frame(funds_chart);
            chart_frame(cashflow_chart);
            if (!finance_history.empty()) {
                const auto [min_funds, max_funds] = std::minmax_element(
                    finance_history.begin(), finance_history.end(),
                    [](const FinanceSample& a, const FinanceSample& b) {
                        return a.funds < b.funds;
                    });
                const float low = static_cast<float>(min_funds->funds);
                const float high = std::max(low + 1.0f, static_cast<float>(max_funds->funds));
                int maximum_flow = 1;
                for (const auto& sample : finance_history)
                    maximum_flow = std::max({maximum_flow, sample.income, sample.upkeep});
                const float flow_max = static_cast<float>(maximum_flow);
                const auto plot_y = [](SDL_FRect area, float value, float min_value,
                                       float max_value) {
                    const float ratio = (value - min_value) /
                        std::max(1.0f, max_value - min_value);
                    return area.y + area.h - ratio * area.h;
                };
                for (std::size_t index = 1; index < finance_history.size(); ++index) {
                    const float x0 = funds_chart.x + funds_chart.w *
                        static_cast<float>(index - 1) / (finance_history.size() - 1);
                    const float x1 = funds_chart.x + funds_chart.w *
                        static_cast<float>(index) / (finance_history.size() - 1);
                    SDL_SetRenderDrawColor(renderer, 105, 184, 114, 255);
                    SDL_RenderLine(renderer, x0,
                        plot_y(funds_chart, static_cast<float>(finance_history[index - 1].funds), low, high),
                        x1, plot_y(funds_chart, static_cast<float>(finance_history[index].funds), low, high));
                    const float in0 = cashflow_chart.y + cashflow_chart.h -
                        finance_history[index - 1].income / flow_max * cashflow_chart.h;
                    const float in1 = cashflow_chart.y + cashflow_chart.h -
                        finance_history[index].income / flow_max * cashflow_chart.h;
                    const float out0 = cashflow_chart.y + cashflow_chart.h -
                        finance_history[index - 1].upkeep / flow_max * cashflow_chart.h;
                    const float out1 = cashflow_chart.y + cashflow_chart.h -
                        finance_history[index].upkeep / flow_max * cashflow_chart.h;
                    SDL_SetRenderDrawColor(renderer, 91, 173, 187, 255);
                    SDL_RenderLine(renderer, x0 - funds_chart.x + cashflow_chart.x, in0,
                                   x1 - funds_chart.x + cashflow_chart.x, in1);
                    SDL_SetRenderDrawColor(renderer, 218, 126, 103, 255);
                    SDL_RenderLine(renderer, x0 - funds_chart.x + cashflow_chart.x, out0,
                                   x1 - funds_chart.x + cashflow_chart.x, out1);
                }
                if (finance_history.size() == 1) {
                    const auto& sample = finance_history.front();
                    SDL_SetRenderDrawColor(renderer, 105, 184, 114, 255);
                    SDL_FRect funds_point{funds_chart.x + funds_chart.w * 0.5f - 3,
                        plot_y(funds_chart, static_cast<float>(sample.funds), low, high) - 3,
                        6, 6};
                    SDL_RenderFillRect(renderer, &funds_point);
                    SDL_SetRenderDrawColor(renderer, 91, 173, 187, 255);
                    SDL_FRect income_point{cashflow_chart.x + cashflow_chart.w * 0.5f - 3,
                        cashflow_chart.y + cashflow_chart.h -
                            sample.income / flow_max * cashflow_chart.h - 3, 6, 6};
                    SDL_RenderFillRect(renderer, &income_point);
                    SDL_SetRenderDrawColor(renderer, 218, 126, 103, 255);
                    SDL_FRect expense_point{cashflow_chart.x + cashflow_chart.w * 0.5f - 3,
                        cashflow_chart.y + cashflow_chart.h -
                            sample.upkeep / flow_max * cashflow_chart.h - 3, 6, 6};
                    SDL_RenderFillRect(renderer, &expense_point);
                }
            }
            SDL_SetRenderDrawColor(renderer, 202, 221, 189, 255);
            SDL_RenderDebugText(renderer, 110, 693, "FUNDS BY WEEK");
            SDL_SetRenderDrawColor(renderer, 91, 173, 187, 255);
            SDL_RenderDebugText(renderer, 540, 693, "INCOME");
            SDL_SetRenderDrawColor(renderer, 218, 126, 103, 255);
            SDL_RenderDebugText(renderer, 620, 693, "EXPENSES");
            }
        }
        if (advisor_open && !state.bankrupt) {
            tint(renderer, {static_cast<float>(margin), static_cast<float>(header),
                static_cast<float>(map_width), static_cast<float>(map_height)},
                {9, 16, 18}, 165);
            fill(renderer, advisor_panel, {31, 48, 48});
            SDL_SetRenderDrawColor(renderer, 218, 174, 103, 255);
            SDL_RenderRect(renderer, &advisor_panel);
            fill(renderer, {advisor_panel.x + 1, advisor_panel.y + 1,
                advisor_panel.w - 2, 48}, {66, 61, 47});
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugText(renderer, advisor_panel.x + 22,
                advisor_panel.y + 18, "CITY ADVISOR  |  FINANCIAL WARNING");
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, advisor_panel.x + 24,
                advisor_panel.y + 82, "Miriam, your community management advisor:");
            SDL_SetRenderDrawColor(renderer, 255, 239, 203, 255);
            SDL_RenderDebugText(renderer, advisor_panel.x + 24,
                advisor_panel.y + 116, financial_advice(world).c_str());
            SDL_SetRenderDrawColor(renderer, 218, 174, 103, 255);
            SDL_RenderDebugTextFormat(renderer, advisor_panel.x + 24,
                advisor_panel.y + 158,
                "Funds %d  |  Base weekly net %+d  |  Risk in about %d weeks",
                state.treasury, weekly_base_net, warning_weeks);
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, advisor_panel.x + 24,
                advisor_panel.y + 194,
                state.loan_balance > 0
                    ? "An active loan already adds a payment every four weeks."
                    : "A loan adds 200 now, with monthly principal and interest payments.");
            draw_button(renderer, advisor_loan_button,
                state.loan_balance > 0 ? "LOAN ACTIVE" : "BORROW 200",
                state.loan_balance > 0);
            draw_button(renderer, advisor_budget_button, "OPEN BUDGET");
            draw_button(renderer, advisor_resume_button, "RESUME TOWN");
            SDL_SetRenderDrawColor(renderer, 180, 202, 187, 255);
            SDL_RenderDebugText(renderer, advisor_panel.x + 24,
                advisor_panel.y + 286, "ESC CLOSES  |  SPACE RESUMES");
        }
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
            world.snapshot().treasury != 217 || !budget_open || options_open)) {
            std::fprintf(stderr, "Zoning toolbar or tax control failed\n");
            exit_status = 1;
        }
        if (toolbar_smoke && (!simulation_started || !paused ||
            world.snapshot().tax_policy != grace::TaxPolicy::high ||
            selected != grace::Tile::pantry ||
            !budget_open || options_open)) {
            std::fprintf(stderr, "Toolbar and options controls failed\n");
            exit_status = 1;
        }
        if (power_smoke && (selected != grace::Tile::power_plant ||
            world.tile(13, 9) != grace::Tile::power_plant ||
            world.snapshot().power_capacity != 12 ||
            !world.is_powered(10, 7))) {
            std::fprintf(stderr, "Power toolbar or plant placement failed\n");
            exit_status = 1;
        }
        if (traffic_smoke && (!upgrade_mode ||
            !world.is_upgraded_road(12, 8) ||
            world.traffic_at(12, 8) <= 6 ||
            world.road_capacity(12, 8) != 14)) {
            std::fprintf(stderr, "Traffic toolbar or road upgrade failed\n");
            exit_status = 1;
        }
        if (save_smoke && (world.tile(13, 9) != grace::Tile::power_plant ||
            world.tile(16, 8) != grace::Tile::road ||
            world.snapshot().treasury != 215 ||
            selected != grace::Tile::road || !paused || simulation_started ||
            !std::filesystem::exists(save_file))) {
            std::fprintf(stderr, "Save/load round trip failed\n");
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
        if (pan_smoke && (camera.offset_x > -100.0f || dragging ||
            !hand_mode || selected != grace::Tile::road)) {
            std::fprintf(stderr, "Right-drag pan or hand selection failed\n");
            exit_status = 1;
        }
        if (bridge_smoke && (world.tile(20, 4) != grace::Tile::road ||
            world.terrain(20, 4) != grace::Terrain::water ||
            !world.is_connected(23, 4) || world.snapshot().treasury != 100)) {
            std::fprintf(stderr, "Bridge drag smoke test failed\n");
            exit_status = 1;
        }
        if (smoke || capture || drag_smoke || pan_smoke || bridge_smoke || zone_smoke || toolbar_smoke || power_smoke ||
            traffic_smoke || save_smoke ||
            bankrupt_smoke ||
            (soak_seconds > 0 && now - started >= static_cast<Uint64>(soak_seconds) * 1000))
            running = false;
        SDL_Delay(16);
    }

    if (logo) SDL_DestroyTexture(logo);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (save_smoke) {
        std::error_code error;
        std::filesystem::remove(save_file, error);
    }
    if (soak_seconds > 0) {
        const auto state = world.snapshot();
        std::printf("soak seconds=%d week=%d population=%d joy=%d funds=%d events=%d\n",
                    soak_seconds, state.week, state.population, state.happiness,
                    state.treasury, state.events_hosted);
    }
    return exit_status;
}
