#include "grace/world.hpp"

#include <iostream>
#include <sstream>

namespace {

bool build(grace::World& world, int x, int y, grace::Tile type) {
    if (world.place(x, y, type)) return true;
    std::cerr << "Could not build " << grace::World::name(type)
              << " at " << x << ',' << y << '\n';
    return false;
}

bool run_weeks(grace::World& world, int weeks) {
    for (int i = 0; i < weeks; ++i) {
        world.advance_week();
        const auto state = world.snapshot();
        if (state.population < 0 || state.population > state.capacity ||
            state.happiness < 0 || state.happiness > 100 ||
            state.volunteers_used > state.volunteers_available) return false;
    }
    return true;
}

void report(const char* name, const grace::World& world) {
    const auto s = world.snapshot();
    std::cout << name << ": week=" << s.week << " population=" << s.population
              << " joy=" << s.happiness << " care=" << s.care
              << " belonging=" << s.belonging << " funds=" << s.treasury
              << " in=" << s.weekly_income << " out=" << s.weekly_upkeep
              << " connected=" << s.connected_homes << " served=" << s.served_homes
              << " power=" << s.powered_demand << '/' << s.power_capacity
              << " need=" << s.power_demand
              << " volunteers=" << s.volunteers_used << '/' << s.volunteers_available
              << '\n';
}

} // namespace

int main() {
    grace::World compact;
    if (!build(compact, 15, 7, grace::Tile::power_plant) ||
        !build(compact, 11, 9, grace::Tile::pantry) ||
        !build(compact, 13, 9, grace::Tile::park) ||
        !build(compact, 9, 7, grace::Tile::home) ||
        !build(compact, 14, 7, grace::Tile::home) ||
        !run_weeks(compact, 24)) return 1;
    report("compact", compact);
    if (compact.snapshot().population < 8 || compact.snapshot().happiness < 70 ||
        compact.snapshot().bankrupt) return 2;

    grace::World spread;
    if (!build(spread, 15, 7, grace::Tile::power_plant) ||
        !build(spread, 16, 8, grace::Tile::road) ||
        !build(spread, 17, 8, grace::Tile::road) ||
        !build(spread, 18, 8, grace::Tile::road) ||
        !build(spread, 16, 7, grace::Tile::home) ||
        !build(spread, 17, 7, grace::Tile::home) ||
        !build(spread, 18, 9, grace::Tile::pantry) ||
        !run_weeks(spread, 24)) return 3;
    report("spread", spread);
    if (spread.snapshot().population < 8 || spread.snapshot().happiness < 70 ||
        spread.snapshot().bankrupt) return 4;

    grace::World event_led;
    if (!build(event_led, 13, 9, grace::Tile::power_plant)) return 5;
    for (int week = 0; week < 24; ++week) {
        if (event_led.can_host_event(grace::EventKind::food_drive) == grace::EventError::none &&
            event_led.host_event(grace::EventKind::food_drive) != grace::EventError::none) return 5;
        if (!run_weeks(event_led, 1)) return 5;
    }
    report("event-led", event_led);
    if (event_led.snapshot().care < 50 || event_led.snapshot().happiness < 70 ||
        event_led.snapshot().bankrupt) return 6;

    grace::World costly_budget;
    if (!build(costly_budget, 11, 9, grace::Tile::pantry) ||
        !build(costly_budget, 13, 7, grace::Tile::chapel) ||
        !build(costly_budget, 13, 9, grace::Tile::park) ||
        !build(costly_budget, 9, 7, grace::Tile::home)) return 11;
    costly_budget.set_giving_policy(grace::GivingPolicy::gentle);
    costly_budget.set_service_budget(grace::ServiceBudget::generous);
    if (!run_weeks(costly_budget, 30) || !costly_budget.snapshot().bankrupt)
        return 11;
    report("costly budget", costly_budget);
    std::stringstream bankrupt_save;
    if (!costly_budget.save(bankrupt_save)) return 11;
    auto restored_bankrupt = grace::World::load(bankrupt_save);
    if (!restored_bankrupt || !restored_bankrupt->snapshot().bankrupt ||
        restored_bankrupt->snapshot().treasury != costly_budget.snapshot().treasury)
        return 11;
    const int final_week = costly_budget.snapshot().week;
    costly_budget.advance_week();
    if (costly_budget.snapshot().week != final_week ||
        costly_budget.place(16, 8, grace::Tile::road) ||
        costly_budget.remove(11, 8)) return 10;

    grace::World zoned_town;
    if (!build(zoned_town, 15, 7, grace::Tile::power_plant) ||
        !build(zoned_town, 9, 7, grace::Tile::residential_zone) ||
        !build(zoned_town, 11, 9, grace::Tile::commercial_zone) ||
        !build(zoned_town, 13, 9, grace::Tile::industrial_zone) ||
        !build(zoned_town, 14, 9, grace::Tile::park) ||
        !run_weeks(zoned_town, 12)) return 12;
    report("zoned town", zoned_town);
    if (zoned_town.snapshot().bankrupt || zoned_town.snapshot().jobs < 10 ||
        zoned_town.snapshot().tax_income <= 0 || zoned_town.snapshot().capacity < 8)
        return 12;

    if (!run_weeks(compact, 456) || !run_weeks(spread, 456)) return 7;
    report("compact long run", compact);
    report("spread long run", spread);
    if (compact.snapshot().bankrupt || spread.snapshot().bankrupt) return 7;

    grace::World expanding;
    if (!build(expanding, 15, 7, grace::Tile::power_plant)) return 8;
    auto build_first = [&](grace::Tile type) {
        for (int y = 0; y < grace::World::height; ++y)
            for (int x = 0; x < grace::World::width; ++x)
                if (expanding.can_place(x, y, type)) return expanding.place(x, y, type);
        return false;
    };
    for (int tick = 0; tick < 480; ++tick) {
        if (!run_weeks(expanding, 1)) return 8;
        const int week = expanding.snapshot().week;
        if (week == 2 && !build(expanding, 11, 9, grace::Tile::pantry)) return 8;
        if (week % 6 == 0 &&
            expanding.can_host_event(grace::EventKind::food_drive) == grace::EventError::none)
            expanding.host_event(grace::EventKind::food_drive);
        if (week % 4 == 0 &&
            expanding.snapshot().capacity - expanding.snapshot().population <= 2)
            build_first(grace::Tile::home);
        if (week % 12 == 0 &&
            expanding.snapshot().power_demand + 3 >= expanding.snapshot().power_capacity)
            build_first(grace::Tile::power_plant);
        if (week % 9 == 0) build_first(grace::Tile::road);
    }
    report("overbuilt long run", expanding);
    if (expanding.snapshot().bankrupt) return 9;
    // The pollution rules can limit growth before demand exceeds supply.
    // Remove plants until the same town experiences a power shortage.
    for (int y = 0; y < grace::World::height &&
         expanding.snapshot().power_demand <= expanding.snapshot().power_capacity; ++y)
        for (int x = 0; x < grace::World::width &&
             expanding.snapshot().power_demand <= expanding.snapshot().power_capacity; ++x)
            if (expanding.tile(x, y) == grace::Tile::power_plant &&
                !expanding.remove(x, y)) return 9;
    if (expanding.snapshot().bankrupt ||
        expanding.snapshot().power_demand <= expanding.snapshot().power_capacity ||
        expanding.snapshot().unpowered_buildings == 0)
        return 9;
    return 0;
}
