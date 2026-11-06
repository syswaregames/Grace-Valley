#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>

namespace grace {

enum class Tile : std::uint8_t {
    meadow, road, home, chapel, pantry, park,
    residential_zone, commercial_zone, industrial_zone, commercial, industrial,
    power_plant
};
enum class Terrain : std::uint8_t { grass, woods, water };
enum class PlacementError : std::uint8_t {
    none, out_of_bounds, occupied, water, insufficient_funds, needs_road, needs_connection, bankrupt
};
enum class EventError : std::uint8_t {
    none, no_people, cooldown, insufficient_funds, insufficient_volunteers, bankrupt
};
enum class EventKind : std::uint8_t { gathering, food_drive };
enum class GivingPolicy : std::uint8_t { gentle, balanced, ambitious };
enum class ServiceBudget : std::uint8_t { lean, balanced, generous };
enum class TaxPolicy : std::uint8_t { low, balanced, high };

struct HouseholdNeeds {
    int belonging = 60;
    int care = 60;
};

struct Snapshot {
    int week = 1;
    int population = 0;
    int capacity = 0;
    int happiness = 50;
    int city_score = 50;
    int treasury = 260;
    int weekly_income = 0;
    int weekly_upkeep = 0;
    int grant_income = 0;
    int giving_income = 0;
    int tax_income = 0;
    int building_upkeep = 0;
    int service_upkeep = 0;
    int community_upkeep = 0;
    int connected_homes = 0;
    int connected_roads = 0;
    int served_homes = 0;
    int belonging = 60;
    int care = 60;
    int volunteers_available = 5;
    int volunteers_used = 0;
    int active_chapels = 0;
    int active_pantries = 0;
    int jobs = 0;
    int shops = 0;
    int workshops = 0;
    int residential_demand = 0;
    int commercial_demand = 0;
    int industrial_demand = 0;
    int power_capacity = 0;
    int power_demand = 0;
    int powered_demand = 0;
    int unpowered_buildings = 0;
    int commuters = 0;
    int noise_affected_homes = 0;
    int congested_roads = 0;
    int peak_traffic = 0;
    int events_hosted = 0;
    int event_weeks_left = 0;
    int event_cooldown = 0;
    EventKind event_kind = EventKind::gathering;
    bool care_first = false;
    bool bankrupt = false;
    GivingPolicy giving_policy = GivingPolicy::balanced;
    ServiceBudget service_budget = ServiceBudget::balanced;
    TaxPolicy tax_policy = TaxPolicy::balanced;
};

class World {
public:
    static constexpr int width = 24;
    static constexpr int height = 16;
    static constexpr int service_radius = 5;

    World();
    Tile tile(int x, int y) const;
    Terrain terrain(int x, int y) const;
    bool is_connected(int x, int y) const;
    bool is_powered(int x, int y) const;
    bool is_noise_polluted(int x, int y) const;
    int traffic_at(int x, int y) const;
    int road_capacity(int x, int y) const;
    int road_level(int x, int y) const;
    int next_road_upgrade_cost(int x, int y) const;
    static const char* road_level_name(int level);
    bool is_upgraded_road(int x, int y) const;
    bool is_active_service(int x, int y) const;
    HouseholdNeeds household_needs(int x, int y) const;
    static bool within_service_range(int source_x, int source_y, int target_x, int target_y);
    bool home_has_service(int x, int y, Tile service) const;
    bool would_serve_home(Tile service, int source_x, int source_y,
                          int home_x, int home_y) const;
    int homes_with_service(Tile service) const;
    int coverage_change_if_placed(Tile service, int x, int y) const;
    PlacementError placement_error(int x, int y, Tile type) const;
    bool can_place(int x, int y, Tile type) const;
    bool place(int x, int y, Tile type);
    bool remove(int x, int y);
    bool upgrade_road(int x, int y);
    void set_care_priority(bool enabled);
    void set_giving_policy(GivingPolicy policy);
    void set_service_budget(ServiceBudget budget);
    void set_tax_policy(TaxPolicy policy);
    EventError can_host_event(EventKind kind = EventKind::gathering) const;
    EventError host_event(EventKind kind = EventKind::gathering);
    void advance_week();
    Snapshot snapshot() const;
    bool save(std::ostream& output) const;
    static std::optional<World> load(std::istream& input);
    static int cost(Tile type);
    int cost_at(int x, int y, Tile type) const;
    static const char* name(Tile type);

private:
    std::array<Tile, width * height> tiles_{};
    std::array<Terrain, width * height> terrain_{};
    std::array<bool, width * height> connected_{};
    std::array<bool, width * height> powered_{};
    std::array<std::uint8_t, width * height> road_levels_{};
    std::array<int, width * height> traffic_{};
    std::array<bool, width * height> staffed_{};
    std::array<HouseholdNeeds, width * height> needs_{};
    Snapshot state_{};
    int last_event_week_ = -100;
    bool inside(int x, int y) const;
    bool has_road(int x, int y) const;
    bool has_adjacent_road(int x, int y) const;
    void rebuild_connections();
    void rebuild_power();
    void rebuild_traffic();
    void rebuild_staffing();
    void update_households();
    void develop_zones(const Snapshot& demand_at_start);
    bool near(int x, int y, Tile type, int radius) const;
    Snapshot evaluate() const;
};

} // namespace grace
