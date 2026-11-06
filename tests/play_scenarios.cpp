#include "grace/world.hpp"

#include <iostream>

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
              << " volunteers=" << s.volunteers_used << '/' << s.volunteers_available
              << '\n';
}

} // namespace

int main() {
    grace::World compact;
    if (!build(compact, 11, 9, grace::Tile::pantry) ||
        !build(compact, 13, 9, grace::Tile::park) ||
        !build(compact, 9, 7, grace::Tile::home) ||
        !build(compact, 14, 7, grace::Tile::home) ||
        !run_weeks(compact, 24)) return 1;
    report("compact", compact);
    if (compact.snapshot().population < 8 || compact.snapshot().happiness < 70 ||
        compact.snapshot().treasury < 0) return 2;

    grace::World spread;
    if (!build(spread, 11, 9, grace::Tile::pantry) ||
        !build(spread, 16, 8, grace::Tile::road) ||
        !build(spread, 17, 8, grace::Tile::road) ||
        !build(spread, 18, 8, grace::Tile::road) ||
        !build(spread, 16, 7, grace::Tile::home) ||
        !build(spread, 17, 7, grace::Tile::home) ||
        !build(spread, 18, 9, grace::Tile::pantry) ||
        !run_weeks(spread, 24)) return 3;
    report("spread", spread);
    if (spread.snapshot().population < 8 || spread.snapshot().happiness < 70 ||
        spread.snapshot().treasury < 0) return 4;

    grace::World event_led;
    for (int week = 0; week < 24; ++week) {
        if (event_led.can_host_event(grace::EventKind::food_drive) == grace::EventError::none &&
            event_led.host_event(grace::EventKind::food_drive) != grace::EventError::none) return 5;
        if (!run_weeks(event_led, 1)) return 5;
    }
    report("event-led", event_led);
    if (event_led.snapshot().care < 50 || event_led.snapshot().happiness < 70 ||
        event_led.snapshot().treasury < 0) return 6;

    if (!run_weeks(compact, 456) || !run_weeks(spread, 456)) return 7;
    report("compact long run", compact);
    report("spread long run", spread);
    return 0;
}
