#include "grace/world.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <queue>
#include <string>
#include <vector>

namespace grace {

namespace {
constexpr int index(int x, int y) { return y * World::width + x; }
constexpr int anchor_x = 12;
constexpr int anchor_y = 8;
constexpr int legacy_width = 24;
constexpr int legacy_height = 16;
constexpr int plant_output = 12;
constexpr std::array<int, 4> road_capacities{6, 14, 24, 36};
constexpr std::array<int, 4> road_upgrade_costs{0, 20, 35, 55};
constexpr std::array<int, 4> road_upkeep{0, 1, 3, 6};
constexpr int power_need(Tile tile) {
    switch (tile) {
    case Tile::home: case Tile::residential_zone: return 1;
    case Tile::commercial: case Tile::commercial_zone: return 2;
    case Tile::industrial: case Tile::industrial_zone: return 3;
    default: return 0;
    }
}
}

World::World() {
    std::fill(tiles_.begin(), tiles_.end(), Tile::meadow);
    std::fill(terrain_.begin(), terrain_.end(), Terrain::grass);
    for (int y = 1; y <= 5; ++y)
        for (int x = 2; x <= 6; ++x)
            if ((x + y) % 4 != 0) terrain_[index(x, y)] = Terrain::woods;
    for (int y = 10; y <= 14; ++y)
        for (int x = 17; x <= 22; ++x)
            if ((x + 2 * y) % 5 != 0) terrain_[index(x, y)] = Terrain::woods;
    for (int y = 3; y <= 6; ++y)
        for (int x = 18; x <= 22; ++x)
            if ((x - 20) * (x - 20) + (y - 4) * (y - 4) <= 6)
                terrain_[index(x, y)] = Terrain::water;
    // Seed the expanded countryside with broad, deterministic woodland and
    // small lakes while leaving the original starting district untouched.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x < legacy_width && y < legacy_height) continue;
            const int woodland = (x * 17 + y * 29 + x * y * 3) % 19;
            if (woodland < 5) terrain_[index(x, y)] = Terrain::woods;
            const int lake_x = (x - 34) * (x - 34);
            const int lake_y = (y - 23) * (y - 23);
            if (lake_x + lake_y <= 12 ||
                ((x - 41) * (x - 41) + (y - 8) * (y - 8) <= 5))
                terrain_[index(x, y)] = Terrain::water;
        }
    }
    for (int x = 8; x <= 15; ++x) tiles_[index(x, 8)] = Tile::road;
    tiles_[index(10, 7)] = Tile::home;
    tiles_[index(12, 7)] = Tile::chapel;
    rebuild_connections();
    rebuild_power();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_traffic();
    state_ = evaluate();
}

bool World::inside(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
}

Tile World::tile(int x, int y) const {
    return inside(x, y) ? tiles_[index(x, y)] : Tile::meadow;
}

Terrain World::terrain(int x, int y) const {
    return inside(x, y) ? terrain_[index(x, y)] : Terrain::grass;
}

bool World::is_connected(int x, int y) const {
    if (!inside(x, y)) return false;
    if (tile(x, y) == Tile::road) return connected_[index(x, y)];
    return tile(x, y) != Tile::meadow && has_road(x, y);
}

bool World::is_powered(int x, int y) const {
    return inside(x, y) && powered_[index(x, y)];
}

bool World::is_noise_polluted(int x, int y) const {
    return inside(x, y) && tile(x, y) == Tile::home &&
           (near(x, y, Tile::industrial, 4) || near(x, y, Tile::power_plant, 4));
}

int World::traffic_at(int x, int y) const {
    return inside(x, y) && tile(x, y) == Tile::road ? traffic_[index(x, y)] : 0;
}

int World::road_capacity(int x, int y) const {
    if (!inside(x, y) || tile(x, y) != Tile::road) return 0;
    return std::max(1, road_capacities[road_levels_[index(x, y)]] *
        static_cast<int>(road_condition_[index(x, y)]) / 100);
}

int World::road_level(int x, int y) const {
    return inside(x, y) && tile(x, y) == Tile::road
        ? road_levels_[index(x, y)] : -1;
}

int World::road_condition(int x, int y) const {
    return inside(x, y) && tile(x, y) == Tile::road
        ? road_condition_[index(x, y)] : 0;
}

int World::bad_road_count() const {
    int count = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            count += tile(x, y) == Tile::road && road_condition(x, y) < 60;
    return count;
}

int World::next_road_upgrade_cost(int x, int y) const {
    const int level = road_level(x, y);
    return level >= 0 && level < 3 ? road_upgrade_costs[level + 1] : 0;
}

const char* World::road_level_name(int level) {
    switch (level) {
    case 0: return "Local road";
    case 1: return "Avenue";
    case 2: return "Boulevard";
    case 3: return "Arterial";
    default: return "No road";
    }
}

bool World::is_upgraded_road(int x, int y) const {
    return road_level(x, y) > 0;
}

bool World::is_active_service(int x, int y) const {
    return inside(x, y) &&
           (tile(x, y) == Tile::chapel || tile(x, y) == Tile::pantry) &&
           staffed_[index(x, y)];
}

HouseholdNeeds World::household_needs(int x, int y) const {
    return inside(x, y) && tile(x, y) == Tile::home
        ? needs_[index(x, y)] : HouseholdNeeds{};
}

bool World::within_service_range(int source_x, int source_y, int target_x, int target_y) {
    return std::abs(source_x - target_x) + std::abs(source_y - target_y) <= service_radius;
}

bool World::home_has_service(int x, int y, Tile service) const {
    return (service == Tile::chapel || service == Tile::pantry) &&
           inside(x, y) && tile(x, y) == Tile::home && has_road(x, y) &&
           near(x, y, service, service_radius);
}

bool World::would_serve_home(Tile service, int source_x, int source_y,
                             int home_x, int home_y) const {
    if ((service != Tile::chapel && service != Tile::pantry) ||
        !can_place(source_x, source_y, service) ||
        !inside(home_x, home_y) || tile(home_x, home_y) != Tile::home ||
        !within_service_range(source_x, source_y, home_x, home_y)) return false;
    World preview = *this;
    preview.place(source_x, source_y, service);
    return preview.is_active_service(source_x, source_y) &&
           preview.home_has_service(home_x, home_y, service);
}

int World::homes_with_service(Tile service) const {
    int count = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            count += home_has_service(x, y, service);
    return count;
}

int World::coverage_change_if_placed(Tile service, int x, int y) const {
    if ((service != Tile::chapel && service != Tile::pantry) ||
        !can_place(x, y, service)) return 0;
    World preview = *this;
    preview.place(x, y, service);
    return preview.homes_with_service(service) - homes_with_service(service);
}

void World::rebuild_connections() {
    std::fill(connected_.begin(), connected_.end(), false);
    if (tile(anchor_x, anchor_y) != Tile::road) return;
    std::queue<int> pending;
    pending.push(index(anchor_x, anchor_y));
    connected_[index(anchor_x, anchor_y)] = true;
    while (!pending.empty()) {
        const int current = pending.front();
        pending.pop();
        const int x = current % width;
        const int y = current / width;
        constexpr int dx[] = {-1, 1, 0, 0};
        constexpr int dy[] = {0, 0, -1, 1};
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction];
            const int ny = y + dy[direction];
            if (inside(nx, ny) && tile(nx, ny) == Tile::road &&
                !connected_[index(nx, ny)]) {
                connected_[index(nx, ny)] = true;
                pending.push(index(nx, ny));
            }
        }
    }
}

void World::rebuild_power() {
    std::fill(powered_.begin(), powered_.end(), false);
    int available = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (tile(x, y) == Tile::power_plant && has_road(x, y)) {
                powered_[index(x, y)] = true;
                available += plant_output;
            }
    // Existing buildings take priority. Vacant zones show whether spare power
    // can support their next building without reserving it in advance.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const Tile type = tile(x, y);
    if (type == Tile::home || type == Tile::commercial || type == Tile::industrial) {
                const int need = power_need(type);
                if (has_road(x, y) && available >= need) {
                    powered_[index(x, y)] = true;
                    available -= need;
                }
            }
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const Tile type = tile(x, y);
            if (type == Tile::residential_zone || type == Tile::commercial_zone ||
                type == Tile::industrial_zone)
                powered_[index(x, y)] = has_road(x, y) && available >= power_need(type);
        }
}

void World::rebuild_traffic() {
    std::fill(traffic_.begin(), traffic_.end(), 0);
    struct Job { int x; int y; int remaining; };
    std::vector<Job> jobs;
    std::vector<int> homes;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const Tile type = tile(x, y);
            if (!powered_[index(x, y)]) continue;
            if (type == Tile::home) homes.push_back(index(x, y));
            else if (type == Tile::commercial || type == Tile::industrial)
                jobs.push_back({x, y, type == Tile::commercial ? 4 : 6});
        }
    if (homes.empty() || jobs.empty()) return;
    std::vector<int> residents(homes.size(), 0);
    for (int person = 0; person < state_.population; ++person) {
        bool assigned = false;
        for (std::size_t offset = 0; offset < homes.size(); ++offset) {
            const std::size_t home = (static_cast<std::size_t>(person) + offset) % homes.size();
            if (residents[home] < 4) {
                ++residents[home];
                assigned = true;
                break;
            }
        }
        if (!assigned) break;
    }
    constexpr int dx[] = {-1, 1, 0, 0};
    constexpr int dy[] = {0, 0, -1, 1};
    for (std::size_t home = 0; home < homes.size(); ++home) {
        if (residents[home] == 0) continue;
        std::array<int, width * height> distance;
        std::array<int, width * height> parent;
        distance.fill(-1);
        parent.fill(-1);
        std::queue<int> pending;
        const int hx = homes[home] % width;
        const int hy = homes[home] / width;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = hx + dx[direction], ny = hy + dy[direction];
            if (inside(nx, ny) && tile(nx, ny) == Tile::road &&
                connected_[index(nx, ny)] && distance[index(nx, ny)] < 0) {
                distance[index(nx, ny)] = 0;
                pending.push(index(nx, ny));
            }
        }
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            const int x = current % width, y = current / width;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction], ny = y + dy[direction];
                if (inside(nx, ny) && tile(nx, ny) == Tile::road &&
                    connected_[index(nx, ny)] && distance[index(nx, ny)] < 0) {
                    distance[index(nx, ny)] = distance[current] + 1;
                    parent[index(nx, ny)] = current;
                    pending.push(index(nx, ny));
                }
            }
        }
        for (int commuter = 0; commuter < residents[home]; ++commuter) {
            int best_job = -1, best_road = -1, best_distance = width * height;
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                if (jobs[j].remaining == 0) continue;
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = jobs[j].x + dx[direction];
                    const int ny = jobs[j].y + dy[direction];
                    if (inside(nx, ny) && tile(nx, ny) == Tile::road &&
                        distance[index(nx, ny)] >= 0 &&
                        distance[index(nx, ny)] < best_distance) {
                        best_distance = distance[index(nx, ny)];
                        best_job = static_cast<int>(j);
                        best_road = index(nx, ny);
                    }
                }
            }
            if (best_job < 0) break;
            --jobs[static_cast<std::size_t>(best_job)].remaining;
            for (int road = best_road; road >= 0; road = parent[road])
                ++traffic_[static_cast<std::size_t>(road)];
        }
    }
}

void World::rebuild_staffing() {
    std::fill(staffed_.begin(), staffed_.end(), false);
    state_.volunteers_available = 5 + state_.population / 4;
    state_.volunteers_used = state_.event_weeks_left > 0 ? 2 : 0;
    state_.active_chapels = 0;
    state_.active_pantries = 0;
    int remaining = state_.volunteers_available - state_.volunteers_used;
    const Tile first = state_.care_first ? Tile::pantry : Tile::chapel;
    const Tile second = state_.care_first ? Tile::chapel : Tile::pantry;
    for (Tile service : {first, second}) {
        const int needed = service == Tile::pantry ? 3 : 2;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (tile(x, y) == service && has_road(x, y) && remaining >= needed) {
                    staffed_[index(x, y)] = true;
                    remaining -= needed;
                    state_.volunteers_used += needed;
                    if (service == Tile::chapel) ++state_.active_chapels;
                    else ++state_.active_pantries;
                }
            }
        }
    }
}

void World::update_households() {
    const int service_gain = state_.service_budget == ServiceBudget::lean ? 3 :
        state_.service_budget == ServiceBudget::generous ? 11 : 7;
    const int giving_effect = state_.giving_policy == GivingPolicy::gentle ? 1 :
        state_.giving_policy == GivingPolicy::ambitious ? -3 : 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (tile(x, y) != Tile::home) continue;
            auto& need = needs_[index(x, y)];
            const bool connected = has_road(x, y);
            const int park = connected && near(x, y, Tile::park, 4)
                ? (state_.service_budget == ServiceBudget::lean ? 1 :
                   state_.service_budget == ServiceBudget::generous ? 3 : 2) : 0;
            const bool workshop = connected && near(x, y, Tile::industrial, 4);
            const bool plant = connected && near(x, y, Tile::power_plant, 4);
            const int pollution = (workshop ? 6 : 0) + (plant ? 6 : 0);
            const int noise = (workshop ? 3 : 0) + (plant ? 3 : 0);
            const bool gathering = state_.event_weeks_left > 0 &&
                                   state_.event_kind == EventKind::gathering;
            const bool food_drive = state_.event_weeks_left > 0 &&
                                    state_.event_kind == EventKind::food_drive;
            need.belonging = std::clamp(need.belonging +
                (home_has_service(x, y, Tile::chapel) ? service_gain : -5) +
                giving_effect + park +
                (gathering ? 10 : 0) + (food_drive ? 1 : 0) - noise -
                (connected ? 0 : 8), 0, 100);
            need.care = std::clamp(need.care +
                (home_has_service(x, y, Tile::pantry) ? service_gain : -5) +
                giving_effect + park +
                (food_drive ? 16 : 0) + (gathering ? 2 : 0) -
                (connected ? 0 : 8) - pollution, 0, 100);
        }
    }
}

int World::cost(Tile type) {
    switch (type) {
    case Tile::road: return 5;
    case Tile::home: return 30;
    case Tile::chapel: return 80;
    case Tile::pantry: return 60;
    case Tile::park: return 25;
    case Tile::residential_zone: return 8;
    case Tile::commercial_zone: return 12;
    case Tile::industrial_zone: return 15;
    case Tile::power_plant: return 40;
    default: return 0;
    }
}

int World::cost_at(int x, int y, Tile type) const {
    return cost(type) + (inside(x, y) && terrain(x, y) == Terrain::woods ? 10 : 0) +
           (inside(x, y) && terrain(x, y) == Terrain::water && type == Tile::road
                ? 20 : 0);
}

const char* World::name(Tile type) {
    switch (type) {
    case Tile::road: return "Road";
    case Tile::home: return "Home";
    case Tile::chapel: return "Chapel";
    case Tile::pantry: return "Pantry";
    case Tile::park: return "Park";
    case Tile::residential_zone: return "Residential zone";
    case Tile::commercial_zone: return "Commercial zone";
    case Tile::industrial_zone: return "Industrial zone";
    case Tile::commercial: return "Shop";
    case Tile::industrial: return "Workshop";
    case Tile::power_plant: return "Power plant";
    case Tile::abandoned_home: return "Abandoned home";
    case Tile::abandoned_commercial: return "Abandoned shop";
    case Tile::abandoned_industrial: return "Abandoned workshop";
    default: return "Meadow";
    }
}

bool World::has_road(int x, int y) const {
    return (inside(x - 1, y) && connected_[index(x - 1, y)]) ||
           (inside(x + 1, y) && connected_[index(x + 1, y)]) ||
           (inside(x, y - 1) && connected_[index(x, y - 1)]) ||
           (inside(x, y + 1) && connected_[index(x, y + 1)]);
}

bool World::has_adjacent_road(int x, int y) const {
    return tile(x - 1, y) == Tile::road || tile(x + 1, y) == Tile::road ||
           tile(x, y - 1) == Tile::road || tile(x, y + 1) == Tile::road;
}

bool World::near(int x, int y, Tile type, int radius) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (std::abs(dx) + std::abs(dy) > radius || !inside(nx, ny) ||
                tile(nx, ny) != type) continue;
            if (type == Tile::chapel || type == Tile::pantry) {
                if (staffed_[index(nx, ny)]) return true;
            } else if (type == Tile::road) {
                if (connected_[index(nx, ny)]) return true;
            } else if (has_road(nx, ny)) return true;
        }
    }
    return false;
}

PlacementError World::placement_error(int x, int y, Tile type) const {
    if (state_.bankrupt) return PlacementError::bankrupt;
    if (!inside(x, y)) return PlacementError::out_of_bounds;
    if (type == Tile::meadow || type == Tile::commercial ||
        type == Tile::industrial || type == Tile::abandoned_home ||
        type == Tile::abandoned_commercial || type == Tile::abandoned_industrial ||
        tile(x, y) != Tile::meadow)
        return PlacementError::occupied;
    if (terrain(x, y) == Terrain::water && type != Tile::road)
        return PlacementError::water;
    if (state_.treasury < cost_at(x, y, type)) return PlacementError::insufficient_funds;
    const bool zone = type == Tile::residential_zone ||
                      type == Tile::commercial_zone || type == Tile::industrial_zone;
    if (!zone && (type == Tile::road ? !has_adjacent_road(x, y) : !has_road(x, y)))
        return type == Tile::road ? PlacementError::needs_connection : PlacementError::needs_road;
    return PlacementError::none;
}

bool World::can_place(int x, int y, Tile type) const {
    return placement_error(x, y, type) == PlacementError::none;
}

bool World::place(int x, int y, Tile type) {
    if (!can_place(x, y, type)) return false;
    tiles_[index(x, y)] = type;
    if (type == Tile::road) road_condition_[index(x, y)] = 100;
    state_.treasury -= cost_at(x, y, type);
    if (terrain_[index(x, y)] != Terrain::water)
        terrain_[index(x, y)] = Terrain::grass;
    rebuild_connections();
    rebuild_power();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_traffic();
    state_ = evaluate();
    return true;
}

bool World::remove(int x, int y) {
    if (state_.bankrupt || !inside(x, y) || tile(x, y) == Tile::meadow ||
        (x == anchor_x && y == anchor_y)) return false;
    tiles_[index(x, y)] = Tile::meadow;
    road_levels_[index(x, y)] = 0;
    road_condition_[index(x, y)] = 100;
    needs_[index(x, y)] = HouseholdNeeds{};
    rebuild_connections();
    rebuild_power();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_traffic();
    state_ = evaluate();
    return true;
}

bool World::upgrade_road(int x, int y) {
    const int cost = next_road_upgrade_cost(x, y);
    if (state_.bankrupt || cost == 0 || state_.treasury < cost)
        return false;
    ++road_levels_[index(x, y)];
    state_.treasury -= cost;
    state_ = evaluate();
    return true;
}

Snapshot World::evaluate() const {
    Snapshot next = state_;
    next.event_cooldown = std::max(0, 6 - (next.week - last_event_week_));
    next.capacity = 0;
    next.connected_homes = 0;
    next.connected_roads = 0;
    next.served_homes = 0;
    next.shops = 0;
    next.workshops = 0;
    next.power_capacity = 0;
    next.power_demand = 0;
    next.powered_demand = 0;
    next.unpowered_buildings = 0;
    next.commuters = 0;
    next.congested_roads = 0;
    next.bad_roads = bad_road_count();
    next.abandoned_buildings = 0;
    next.average_road_condition = 0;
    next.peak_traffic = 0;
    next.noise_affected_homes = 0;
    int parks = 0;
    int connected_parks = 0;
    int park_exposure = 0;
    int homes = 0;
    int roads = 0;
    int bridges = 0;
    int all_shops = 0;
    int all_workshops = 0;
    int chapels = 0;
    int pantries = 0;
    int plants = 0;
    int road_upgrade_upkeep = 0;
    int total_road_condition = 0;
    int total_belonging = 0;
    int total_care = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            switch (tile(x, y)) {
            case Tile::road:
                ++roads;
                bridges += terrain(x, y) == Terrain::water;
                road_upgrade_upkeep += road_upkeep[road_levels_[index(x, y)]];
                total_road_condition += road_condition_[index(x, y)];
                if (connected_[index(x, y)]) ++next.connected_roads;
                next.peak_traffic = std::max(next.peak_traffic, traffic_[index(x, y)]);
                if (traffic_[index(x, y)] > road_capacity(x, y))
                    ++next.congested_roads;
                break;
            case Tile::home:
                ++homes;
                if (is_noise_polluted(x, y)) ++next.noise_affected_homes;
                next.power_demand += 1;
                if (powered_[index(x, y)]) ++next.powered_demand;
                else ++next.unpowered_buildings;
                total_belonging += needs_[index(x, y)].belonging;
                total_care += needs_[index(x, y)].care;
                if (has_road(x, y) && powered_[index(x, y)]) {
                    ++next.connected_homes;
                    next.capacity += 4;
                    if (home_has_service(x, y, Tile::chapel) &&
                        home_has_service(x, y, Tile::pantry))
                        ++next.served_homes;
                    if (near(x, y, Tile::park, 4)) ++park_exposure;
                }
                break;
            case Tile::chapel: ++chapels; break;
            case Tile::pantry: ++pantries; break;
            case Tile::park:
                ++parks;
                if (has_road(x, y)) ++connected_parks;
                break;
            case Tile::commercial:
                ++all_shops;
                next.power_demand += 2;
                if (powered_[index(x, y)]) { next.powered_demand += 2; ++next.shops; }
                else ++next.unpowered_buildings;
                break;
            case Tile::industrial:
                ++all_workshops;
                next.power_demand += 3;
                if (powered_[index(x, y)]) { next.powered_demand += 3; ++next.workshops; }
                else ++next.unpowered_buildings;
                break;
            case Tile::abandoned_home:
            case Tile::abandoned_commercial:
            case Tile::abandoned_industrial:
                ++next.abandoned_buildings;
                break;
            case Tile::power_plant:
                ++plants;
                if (powered_[index(x, y)]) next.power_capacity += plant_output;
                break;
            default: break;
            }
        }
    }
    next.population = std::min(next.population, next.capacity);
    next.grant_income = 10;
    next.giving_income = next.giving_policy == GivingPolicy::gentle
        ? next.population : next.giving_policy == GivingPolicy::ambitious
            ? next.population * 2 : next.population * 3 / 2;
    next.tax_income = next.tax_policy == TaxPolicy::low
        ? next.shops * 3 + next.workshops * 5 :
        next.tax_policy == TaxPolicy::high
            ? next.shops * 9 + next.workshops * 14 :
              next.shops * 6 + next.workshops * 9;
    next.weekly_income = next.grant_income + next.giving_income + next.tax_income;
    const int road_base_upkeep = (roads + 2) / 3 + bridges + road_upgrade_upkeep;
    next.road_upkeep = next.road_budget == RoadBudget::lean
        ? (road_base_upkeep + 1) / 2 : next.road_budget == RoadBudget::generous
            ? road_base_upkeep * 2 : road_base_upkeep;
    next.average_road_condition = roads == 0 ? 100 : total_road_condition / roads;
    next.building_upkeep = homes +
                           all_shops * 2 + all_workshops * 3 +
                           next.abandoned_buildings;
    next.building_upkeep += plants * 6;
    const int base_services = chapels * 4 + pantries * 3 + parks;
    next.service_upkeep = next.service_budget == ServiceBudget::lean
        ? (base_services + 1) / 2 : next.service_budget == ServiceBudget::generous
            ? base_services * 2 : base_services;
    next.community_upkeep = next.population / 2 +
                            next.population * next.population / 150;
    next.loan_interest = next.loan_balance > 0
        ? (next.loan_balance * 5 + 99) / 100 : 0;
    next.monthly_loan_payment = next.loan_balance > 0
        ? std::min(20, next.loan_balance) + next.loan_interest : 0;
    next.loan_due_this_week = next.week % 4 == 0 ? next.monthly_loan_payment : 0;
    next.weekly_upkeep = next.building_upkeep + next.road_upkeep + next.service_upkeep +
                         next.community_upkeep + next.loan_due_this_week;
    next.belonging = homes == 0 ? 60 : total_belonging / homes;
    next.care = homes == 0 ? 60 : total_care / homes;
    next.happiness = std::clamp((next.belonging + next.care) / 2 +
                                    std::min(park_exposure *
                                        (next.service_budget == ServiceBudget::lean ? 1 :
                                         next.service_budget == ServiceBudget::generous ? 3 : 2), 10) +
                                    (next.event_weeks_left > 0 ?
                                        (next.event_kind == EventKind::gathering ? 8 : 4) : 0) -
                                    (next.connected_homes < homes ? 10 : 0) -
                                    std::min(next.congested_roads * 2, 12) -
                                    std::min(next.workshops * 2, 12), 0, 100);
    next.jobs = next.shops * 4 + next.workshops * 6;
    next.commuters = std::min(next.population, next.jobs);
    // City Score captures longer-term desirability. Congestion also affects
    // immediate happiness above, so a bottleneck has a visible growth cost.
    next.city_score = std::clamp(45 + next.happiness / 2 +
        std::min(connected_parks * 3, 12) +
        std::min(next.served_homes * 2, 10) -
        std::min(next.congested_roads * 4, 28) -
        std::min(next.noise_affected_homes * 5, 25) -
        std::min(next.bad_roads * 2, 20) -
        std::min(next.abandoned_buildings * 3, 24) -
        std::min(next.unpowered_buildings * 6, 24) -
        (next.connected_homes < homes ? 10 : 0) -
        (next.weekly_upkeep > next.weekly_income ? 6 : 0), 0, 100);
    const int quality_effect = next.city_score - 60;
    const int tax_growth = next.tax_policy == TaxPolicy::low ? 12 :
                           next.tax_policy == TaxPolicy::high ? -18 : 0;
    next.residential_demand = std::clamp(45 + next.jobs * 5 +
        connected_parks * 8 + next.happiness / 5 - next.population * 3 -
        (next.capacity - next.population) * 4 + quality_effect / 2 -
        std::min(next.noise_affected_homes * 8, 40), 0, 100);
    next.commercial_demand = std::clamp(20 + next.population * 4 -
        next.shops * 30 - next.workshops * 3 + tax_growth +
        quality_effect / 3, 0, 100);
    next.industrial_demand = std::clamp(15 + next.population * 3 +
        next.shops * 5 - next.workshops * 35 + tax_growth +
        quality_effect / 4, 0, 100);
    return next;
}

void World::develop_zones(const Snapshot& demand_at_start) {
    auto develop_one = [&](Tile zone, Tile building, int demand) {
        if (demand < 25) return;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (tile(x, y) == zone && powered_[index(x, y)]) {
                    tiles_[index(x, y)] = building;
                    if (building == Tile::home) needs_[index(x, y)] = HouseholdNeeds{};
                    rebuild_power();
                    return;
                }
            }
        }
    };
    develop_one(Tile::residential_zone, Tile::home, demand_at_start.residential_demand);
    develop_one(Tile::commercial_zone, Tile::commercial, demand_at_start.commercial_demand);
    develop_one(Tile::industrial_zone, Tile::industrial, demand_at_start.industrial_demand);
}

void World::advance_week() {
    if (state_.bankrupt) return;
    rebuild_staffing();
    state_ = evaluate();
    const Snapshot demand_at_start = state_;
    const bool year_end = state_.week % 52 == 0;
    state_.year_income += state_.weekly_income;
    state_.year_expenses += state_.weekly_upkeep;
    ++state_.year_weeks_recorded;
    state_.treasury += state_.weekly_income - state_.weekly_upkeep;
    if (state_.treasury < 0) {
        state_.bankrupt = true;
        ++state_.week;
        return;
    }
    if (state_.loan_due_this_week > 0)
        state_.loan_balance -= std::min(20, state_.loan_balance);
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i] != Tile::road) continue;
        if (state_.road_budget == RoadBudget::lean)
            road_condition_[i] = static_cast<std::uint8_t>(
                std::max(0, static_cast<int>(road_condition_[i]) - 12));
        else if (state_.road_budget == RoadBudget::generous)
            road_condition_[i] = static_cast<std::uint8_t>(
                std::min(100, static_cast<int>(road_condition_[i]) + 8));
    }
    const int arrival = (state_.happiness >= 55 ? 2 : 1) +
        (state_.event_weeks_left > 0 && state_.event_kind == EventKind::gathering ? 1 : 0);
    state_.population = std::min(state_.capacity, state_.population + arrival);
    state_ = evaluate();
    update_households();
    state_ = evaluate();
    develop_zones(demand_at_start);
    rebuild_power();
    if (state_.event_weeks_left > 0) --state_.event_weeks_left;
    ++state_.week;
    rebuild_staffing();
    state_ = evaluate();
    rebuild_traffic();
    state_ = evaluate();
    if (state_.city_score < 30) {
        recovery_weeks_ = 0;
        if (++low_score_weeks_ >= 3) {
            if (!transition_building_condition(false)) low_score_weeks_ = 0;
            else low_score_weeks_ = 0;
        }
    } else if (state_.city_score >= 45) {
        low_score_weeks_ = 0;
        if (++recovery_weeks_ >= 3) {
            transition_building_condition(true);
            recovery_weeks_ = 0;
        }
    } else {
        low_score_weeks_ = 0;
        recovery_weeks_ = 0;
    }
    state_.low_score_weeks = low_score_weeks_;
    if (state_.abandoned_buildings > 0 || state_.low_score_weeks > 0) {
        rebuild_power();
        rebuild_staffing();
        state_ = evaluate();
        rebuild_traffic();
        state_ = evaluate();
        state_.low_score_weeks = low_score_weeks_;
    }
    if (year_end) {
        state_.last_report_year = (state_.week - 2) / 52 + 1;
        state_.last_year_income = state_.year_income;
        state_.last_year_expenses = state_.year_expenses;
        state_.last_year_opening_funds = state_.year_opening_funds;
        state_.last_year_closing_funds = state_.treasury;
        state_.last_year_population = state_.population;
        state_.last_year_city_score = state_.city_score;
        state_.last_year_weeks_recorded = state_.year_weeks_recorded;
        state_.year_income = 0;
        state_.year_expenses = 0;
        state_.year_opening_funds = state_.treasury;
        state_.year_weeks_recorded = 0;
    }
}

bool World::transition_building_condition(bool recovering) {
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            Tile& structure = tiles_[index(x, y)];
            if (recovering) {
                if (structure == Tile::abandoned_home)
                    structure = Tile::home;
                else if (structure == Tile::abandoned_commercial)
                    structure = Tile::commercial;
                else if (structure == Tile::abandoned_industrial)
                    structure = Tile::industrial;
                else continue;
            } else {
                if (structure == Tile::home)
                    structure = Tile::abandoned_home;
                else if (structure == Tile::commercial)
                    structure = Tile::abandoned_commercial;
                else if (structure == Tile::industrial)
                    structure = Tile::abandoned_industrial;
                else continue;
            }
            return true;
        }
    }
    return false;
}

void World::set_care_priority(bool enabled) {
    if (state_.bankrupt) return;
    state_.care_first = enabled;
    rebuild_staffing();
    state_ = evaluate();
}

void World::set_giving_policy(GivingPolicy policy) {
    if (state_.bankrupt) return;
    state_.giving_policy = policy;
    state_ = evaluate();
}

void World::set_service_budget(ServiceBudget budget) {
    if (state_.bankrupt) return;
    state_.service_budget = budget;
    state_ = evaluate();
}

void World::set_road_budget(RoadBudget budget) {
    if (state_.bankrupt) return;
    state_.road_budget = budget;
    state_ = evaluate();
}

void World::set_tax_policy(TaxPolicy policy) {
    if (state_.bankrupt) return;
    state_.tax_policy = policy;
    state_ = evaluate();
}

bool World::take_loan() {
    if (state_.bankrupt || state_.loan_balance > 0 ||
        state_.treasury > 1000000000 - loan_amount) return false;
    state_.treasury += loan_amount;
    state_.loan_balance = loan_amount;
    state_ = evaluate();
    return true;
}

EventError World::can_host_event(EventKind kind) const {
    if (state_.bankrupt) return EventError::bankrupt;
    if (state_.population < 2) return EventError::no_people;
    if (state_.week - last_event_week_ < 6) return EventError::cooldown;
    if (state_.treasury < (kind == EventKind::food_drive ? 25 : 30))
        return EventError::insufficient_funds;
    if (state_.volunteers_available - state_.volunteers_used < 2)
        return EventError::insufficient_volunteers;
    return EventError::none;
}

EventError World::host_event(EventKind kind) {
    const EventError error = can_host_event(kind);
    if (error != EventError::none) return error;
    state_.treasury -= kind == EventKind::food_drive ? 25 : 30;
    state_.event_weeks_left = 2;
    state_.event_kind = kind;
    ++state_.events_hosted;
    last_event_week_ = state_.week;
    rebuild_staffing();
    state_ = evaluate();
    return EventError::none;
}

Snapshot World::snapshot() const { return state_; }

bool World::save(std::ostream& output) const {
    output << "GRACE_VALLEY_WORLD 6\n";
    output << state_.week << ' ' << state_.population << ' ' << state_.treasury << ' '
           << static_cast<int>(state_.care_first) << ' '
           << static_cast<int>(state_.bankrupt) << ' '
           << static_cast<int>(state_.giving_policy) << ' '
           << static_cast<int>(state_.service_budget) << ' '
           << static_cast<int>(state_.road_budget) << ' '
           << static_cast<int>(state_.tax_policy) << ' '
           << state_.events_hosted << ' ' << state_.event_weeks_left << ' '
           << static_cast<int>(state_.event_kind) << ' ' << last_event_week_ << ' '
           << low_score_weeks_ << ' ' << recovery_weeks_ << ' '
           << state_.loan_balance << ' ' << state_.year_income << ' '
           << state_.year_expenses << ' ' << state_.year_opening_funds << ' '
           << state_.year_weeks_recorded << ' ' << state_.last_report_year << ' '
           << state_.last_year_income << ' ' << state_.last_year_expenses << ' '
           << state_.last_year_opening_funds << ' ' << state_.last_year_closing_funds << ' '
           << state_.last_year_population << ' ' << state_.last_year_city_score << ' '
           << state_.last_year_weeks_recorded << '\n';
    output << width << ' ' << height << '\n';
    for (std::size_t i = 0; i < tiles_.size(); ++i)
        output << static_cast<int>(tiles_[i]) << ' '
               << static_cast<int>(terrain_[i]) << ' '
               << static_cast<int>(road_levels_[i]) << ' '
               << static_cast<int>(road_condition_[i]) << ' '
               << needs_[i].belonging << ' ' << needs_[i].care << '\n';
    return static_cast<bool>(output);
}

std::optional<World> World::load(std::istream& input) {
    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "GRACE_VALLEY_WORLD" ||
        (version < 1 || version > 6))
        return std::nullopt;
    World result;
    int care_first, bankrupt, giving, services, roads = 1, tax, event_kind;
    int low_score_weeks = 0, recovery_weeks = 0;
    if (!(input >> result.state_.week >> result.state_.population >>
          result.state_.treasury >> care_first >> bankrupt >> giving >>
          services)) return std::nullopt;
    if (version >= 3 && !(input >> roads)) return std::nullopt;
    if (!(input >> tax >> result.state_.events_hosted >>
          result.state_.event_weeks_left >> event_kind >> result.last_event_week_))
        return std::nullopt;
    if (version >= 4 && !(input >> low_score_weeks >> recovery_weeks))
        return std::nullopt;
    if (version >= 5 && !(input >> result.state_.loan_balance))
        return std::nullopt;
    if (version >= 6) {
        if (!(input >> result.state_.year_income >> result.state_.year_expenses >>
              result.state_.year_opening_funds >> result.state_.year_weeks_recorded >>
              result.state_.last_report_year >> result.state_.last_year_income >>
              result.state_.last_year_expenses >> result.state_.last_year_opening_funds >>
              result.state_.last_year_closing_funds >> result.state_.last_year_population >>
              result.state_.last_year_city_score >> result.state_.last_year_weeks_recorded))
            return std::nullopt;
    } else result.state_.year_opening_funds = result.state_.treasury;
    if (result.state_.week < 1 || result.state_.week > 100000000 ||
        result.state_.population < 0 || result.state_.population > width * height * 4 ||
        result.state_.treasury < -1000000000 || result.state_.treasury > 1000000000 ||
        care_first < 0 || care_first > 1 || bankrupt < 0 || bankrupt > 1 ||
        giving < 0 || giving > 2 || services < 0 || services > 2 ||
        roads < 0 || roads > 2 ||
        low_score_weeks < 0 || low_score_weeks > 2 ||
        recovery_weeks < 0 || recovery_weeks > 2 ||
        result.state_.loan_balance < 0 || result.state_.loan_balance > loan_amount ||
        result.state_.year_income < 0 || result.state_.year_expenses < 0 ||
        result.state_.last_year_income < 0 || result.state_.last_year_expenses < 0 ||
        result.state_.year_income > 1000000000000LL || result.state_.year_expenses > 1000000000000LL ||
        result.state_.last_year_income > 1000000000000LL || result.state_.last_year_expenses > 1000000000000LL ||
        result.state_.year_opening_funds < -1000000000 || result.state_.year_opening_funds > 1000000000 ||
        result.state_.last_year_opening_funds < -1000000000 || result.state_.last_year_opening_funds > 1000000000 ||
        result.state_.last_year_closing_funds < -1000000000 || result.state_.last_year_closing_funds > 1000000000 ||
        result.state_.year_weeks_recorded < 0 || result.state_.year_weeks_recorded > 52 ||
        result.state_.last_year_weeks_recorded < 0 || result.state_.last_year_weeks_recorded > 52 ||
        result.state_.last_report_year < 0 || result.state_.last_report_year > (result.state_.week - 1) / 52 ||
        result.state_.last_year_population < 0 || result.state_.last_year_population > width * height * 4 ||
        result.state_.last_year_city_score < 0 || result.state_.last_year_city_score > 100 ||
        tax < 0 || tax > 2 || result.state_.events_hosted < 0 ||
        result.state_.event_weeks_left < 0 || result.state_.event_weeks_left > 2 ||
        event_kind < 0 || event_kind > 1 ||
        result.last_event_week_ < -100000000 ||
        result.last_event_week_ > result.state_.week ||
        (bankrupt != 0 && result.state_.treasury >= 0))
        return std::nullopt;
    result.state_.care_first = care_first != 0;
    result.state_.bankrupt = bankrupt != 0;
    result.state_.giving_policy = static_cast<GivingPolicy>(giving);
    result.state_.service_budget = static_cast<ServiceBudget>(services);
    result.state_.road_budget = static_cast<RoadBudget>(roads);
    result.low_score_weeks_ = low_score_weeks;
    result.recovery_weeks_ = recovery_weeks;
    result.state_.low_score_weeks = low_score_weeks;
    result.state_.tax_policy = static_cast<TaxPolicy>(tax);
    result.state_.event_kind = static_cast<EventKind>(event_kind);
    const int saved_population = result.state_.population;
    int saved_width, saved_height;
    if (!(input >> saved_width >> saved_height) ||
        !((saved_width == width && saved_height == height) ||
          (saved_width == legacy_width && saved_height == legacy_height)))
        return std::nullopt;
    for (int y = 0; y < saved_height; ++y) {
        for (int x = 0; x < saved_width; ++x) {
            int tile, terrain, upgraded, condition = 100, belonging, care;
            if (!(input >> tile >> terrain >> upgraded) ||
                (version >= 3 && !(input >> condition)) ||
                !(input >> belonging >> care) ||
                tile < 0 || tile > static_cast<int>(Tile::abandoned_industrial) ||
                terrain < 0 || terrain > static_cast<int>(Terrain::water) ||
                upgraded < 0 || upgraded > (version == 1 ? 1 : 3) ||
                condition < 0 || condition > 100 ||
                (upgraded && tile != static_cast<int>(Tile::road)) ||
                belonging < 0 || belonging > 100 || care < 0 || care > 100 ||
                (terrain == static_cast<int>(Terrain::water) &&
                 tile != static_cast<int>(Tile::meadow) &&
                 tile != static_cast<int>(Tile::road))) return std::nullopt;
            const auto i = static_cast<std::size_t>(index(x, y));
            result.tiles_[i] = static_cast<Tile>(tile);
            result.terrain_[i] = static_cast<Terrain>(terrain);
            result.road_levels_[i] = static_cast<std::uint8_t>(upgraded);
            result.road_condition_[i] = static_cast<std::uint8_t>(condition);
            result.needs_[i] = {belonging, care};
        }
    }
    if (result.tile(anchor_x, anchor_y) != Tile::road) return std::nullopt;
    result.rebuild_connections();
    result.rebuild_power();
    result.rebuild_staffing();
    result.state_ = result.evaluate();
    result.rebuild_staffing();
    result.state_ = result.evaluate();
    result.rebuild_traffic();
    result.state_ = result.evaluate();
    result.state_.low_score_weeks = result.low_score_weeks_;
    if (result.state_.population != saved_population) return std::nullopt;
    return result;
}

} // namespace grace
