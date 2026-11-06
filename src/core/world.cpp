#include "grace/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <queue>

namespace grace {

namespace {
constexpr int index(int x, int y) { return y * World::width + x; }
constexpr int anchor_x = 12;
constexpr int anchor_y = 8;
}

World::World() {
    tiles_.fill(Tile::meadow);
    terrain_.fill(Terrain::grass);
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
    for (int x = 8; x <= 15; ++x) tiles_[index(x, 8)] = Tile::road;
    tiles_[index(10, 7)] = Tile::home;
    tiles_[index(12, 7)] = Tile::chapel;
    rebuild_connections();
    rebuild_staffing();
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
    connected_.fill(false);
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

void World::rebuild_staffing() {
    staffed_.fill(false);
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
            const int pollution = connected && near(x, y, Tile::industrial, 3) ? 3 : 0;
            const bool gathering = state_.event_weeks_left > 0 &&
                                   state_.event_kind == EventKind::gathering;
            const bool food_drive = state_.event_weeks_left > 0 &&
                                    state_.event_kind == EventKind::food_drive;
            need.belonging = std::clamp(need.belonging +
                (home_has_service(x, y, Tile::chapel) ? service_gain : -5) +
                giving_effect + park +
                (gathering ? 10 : 0) + (food_drive ? 1 : 0) -
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
    default: return 0;
    }
}

int World::cost_at(int x, int y, Tile type) const {
    return cost(type) + (inside(x, y) && terrain(x, y) == Terrain::woods ? 10 : 0);
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
        type == Tile::industrial || tile(x, y) != Tile::meadow)
        return PlacementError::occupied;
    if (terrain(x, y) == Terrain::water) return PlacementError::water;
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
    state_.treasury -= cost_at(x, y, type);
    terrain_[index(x, y)] = Terrain::grass;
    rebuild_connections();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_staffing();
    state_ = evaluate();
    return true;
}

bool World::remove(int x, int y) {
    if (state_.bankrupt || !inside(x, y) || tile(x, y) == Tile::meadow ||
        (x == anchor_x && y == anchor_y)) return false;
    tiles_[index(x, y)] = Tile::meadow;
    needs_[index(x, y)] = HouseholdNeeds{};
    rebuild_connections();
    rebuild_staffing();
    state_ = evaluate();
    rebuild_staffing();
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
    int parks = 0;
    int connected_parks = 0;
    int park_exposure = 0;
    int homes = 0;
    int roads = 0;
    int all_shops = 0;
    int all_workshops = 0;
    int chapels = 0;
    int pantries = 0;
    int total_belonging = 0;
    int total_care = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            switch (tile(x, y)) {
            case Tile::road:
                ++roads;
                if (connected_[index(x, y)]) ++next.connected_roads;
                break;
            case Tile::home:
                ++homes;
                total_belonging += needs_[index(x, y)].belonging;
                total_care += needs_[index(x, y)].care;
                if (has_road(x, y)) {
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
                if (has_road(x, y)) ++next.shops;
                break;
            case Tile::industrial:
                ++all_workshops;
                if (has_road(x, y)) ++next.workshops;
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
    next.building_upkeep = homes + (roads + 2) / 3 +
                           all_shops * 2 + all_workshops * 3;
    const int base_services = chapels * 4 + pantries * 3 + parks;
    next.service_upkeep = next.service_budget == ServiceBudget::lean
        ? (base_services + 1) / 2 : next.service_budget == ServiceBudget::generous
            ? base_services * 2 : base_services;
    next.community_upkeep = next.population / 2 +
                            next.population * next.population / 150;
    next.weekly_upkeep = next.building_upkeep + next.service_upkeep +
                         next.community_upkeep;
    next.belonging = homes == 0 ? 60 : total_belonging / homes;
    next.care = homes == 0 ? 60 : total_care / homes;
    next.happiness = std::clamp((next.belonging + next.care) / 2 +
                                    std::min(park_exposure *
                                        (next.service_budget == ServiceBudget::lean ? 1 :
                                         next.service_budget == ServiceBudget::generous ? 3 : 2), 10) +
                                    (next.event_weeks_left > 0 ?
                                        (next.event_kind == EventKind::gathering ? 8 : 4) : 0) -
                                    (next.connected_homes < homes ? 10 : 0) -
                                    std::min(next.workshops * 2, 12), 0, 100);
    next.jobs = next.shops * 4 + next.workshops * 6;
    const int tax_growth = next.tax_policy == TaxPolicy::low ? 12 :
                           next.tax_policy == TaxPolicy::high ? -18 : 0;
    next.residential_demand = std::clamp(45 + next.jobs * 5 +
        connected_parks * 8 + next.happiness / 5 - next.population * 3 -
        (next.capacity - next.population) * 4, 0, 100);
    next.commercial_demand = std::clamp(20 + next.population * 4 -
        next.shops * 30 - next.workshops * 3 + tax_growth, 0, 100);
    next.industrial_demand = std::clamp(15 + next.population * 3 +
        next.shops * 5 - next.workshops * 35 + tax_growth, 0, 100);
    return next;
}

void World::develop_zones(const Snapshot& demand_at_start) {
    auto develop_one = [&](Tile zone, Tile building, int demand) {
        if (demand < 25) return;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (tile(x, y) == zone && has_road(x, y)) {
                    tiles_[index(x, y)] = building;
                    if (building == Tile::home) needs_[index(x, y)] = HouseholdNeeds{};
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
    state_.treasury += state_.weekly_income - state_.weekly_upkeep;
    if (state_.treasury < 0) {
        state_.bankrupt = true;
        ++state_.week;
        return;
    }
    const int arrival = (state_.happiness >= 55 ? 2 : 1) +
        (state_.event_weeks_left > 0 && state_.event_kind == EventKind::gathering ? 1 : 0);
    state_.population = std::min(state_.capacity, state_.population + arrival);
    state_ = evaluate();
    update_households();
    state_ = evaluate();
    develop_zones(demand_at_start);
    if (state_.event_weeks_left > 0) --state_.event_weeks_left;
    ++state_.week;
    rebuild_staffing();
    state_ = evaluate();
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

void World::set_tax_policy(TaxPolicy policy) {
    if (state_.bankrupt) return;
    state_.tax_policy = policy;
    state_ = evaluate();
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

} // namespace grace
