#include "grace/world.hpp"

#ifdef NDEBUG
#undef NDEBUG // Keep these rule checks active in Release builds.
#endif
#include <cassert>
#include <iostream>

int main() {
    grace::World world;
    assert(world.terrain(20, 4) == grace::Terrain::water);
    assert(world.placement_error(20, 4, grace::Tile::road) == grace::PlacementError::water);
    assert(world.terrain(17, 10) == grace::Terrain::woods);
    assert(world.cost_at(17, 10, grace::Tile::road) == grace::World::cost(grace::Tile::road) + 10);
    const auto start = world.snapshot();
    assert(start.week == 1);
    assert(start.capacity == 4);
    assert(start.weekly_income == 10);
    assert(start.weekly_upkeep == 8);
    assert(start.weekly_income == start.grant_income + start.giving_income);
    assert(start.weekly_upkeep == start.building_upkeep + start.service_upkeep +
                                  start.community_upkeep);
    grace::World forecast;
    const auto first_week = forecast.snapshot();
    forecast.advance_week();
    assert(forecast.snapshot().treasury == first_week.treasury +
           first_week.weekly_income - first_week.weekly_upkeep);
    grace::World lean_budget;
    grace::World generous_budget;
    assert(lean_budget.place(11, 9, grace::Tile::pantry));
    assert(generous_budget.place(11, 9, grace::Tile::pantry));
    lean_budget.set_service_budget(grace::ServiceBudget::lean);
    generous_budget.set_service_budget(grace::ServiceBudget::generous);
    assert(lean_budget.snapshot().service_upkeep <
           generous_budget.snapshot().service_upkeep);
    lean_budget.advance_week();
    generous_budget.advance_week();
    assert(generous_budget.household_needs(10, 7).care >
           lean_budget.household_needs(10, 7).care);
    grace::World gentle_giving;
    grace::World ambitious_giving;
    gentle_giving.set_giving_policy(grace::GivingPolicy::gentle);
    ambitious_giving.set_giving_policy(grace::GivingPolicy::ambitious);
    gentle_giving.advance_week();
    ambitious_giving.advance_week();
    assert(ambitious_giving.snapshot().giving_income >
           gentle_giving.snapshot().giving_income);
    assert(ambitious_giving.household_needs(10, 7).belonging <
           gentle_giving.household_needs(10, 7).belonging);
    assert(!world.place(-1, 0, grace::Tile::road));
    assert(!world.place(0, 0, grace::Tile::home));
    assert(world.placement_error(0, 0, grace::Tile::road) ==
           grace::PlacementError::needs_connection);
    assert(!world.remove(12, 8)); // The starting chapel's road anchors the network.
    assert(world.place(9, 7, grace::Tile::home));
    assert(world.snapshot().capacity == 8);
    assert(world.snapshot().treasury == start.treasury - 30);
    assert(world.home_has_service(10, 7, grace::Tile::chapel));
    assert(!world.home_has_service(10, 7, grace::Tile::pantry));
    assert(world.would_serve_home(grace::Tile::pantry, 15, 7, 10, 7));
    assert(!world.would_serve_home(grace::Tile::pantry, 15, 7, 9, 7));
    assert(world.coverage_change_if_placed(grace::Tile::pantry, 15, 7) == 1);
    assert(world.place(11, 9, grace::Tile::pantry));
    assert(world.snapshot().served_homes == 2);
    assert(world.homes_with_service(grace::Tile::pantry) == 2);
    world.advance_week();
    assert(world.snapshot().week == 2);
    assert(world.snapshot().population > 0);
    assert(world.remove(11, 9));
    assert(world.snapshot().served_homes == 0);
    assert(world.remove(11, 8));
    assert(!world.is_connected(10, 8));
    assert(world.snapshot().connected_homes == 0);
    assert(world.snapshot().capacity == 0);
    assert(!world.home_has_service(10, 7, grace::Tile::chapel));
    assert(world.place(12, 9, grace::Tile::road));
    assert(world.place(11, 9, grace::Tile::road));
    assert(world.place(10, 9, grace::Tile::road));
    assert(world.is_connected(10, 8));
    assert(world.snapshot().connected_homes == 2);
    grace::World repair;
    assert(repair.remove(11, 8));
    assert(!repair.is_connected(10, 8));
    assert(repair.can_place(10, 9, grace::Tile::road));
    assert(!repair.can_place(10, 7, grace::Tile::home));
    assert(repair.place(10, 9, grace::Tile::road));
    assert(!repair.is_connected(10, 9));
    assert(repair.place(11, 9, grace::Tile::road));
    assert(!repair.is_connected(11, 9));
    assert(repair.place(12, 9, grace::Tile::road));
    assert(repair.is_connected(10, 8));
    grace::World terrain_world;
    assert(terrain_world.place(16, 8, grace::Tile::road));
    assert(terrain_world.place(17, 8, grace::Tile::road));
    assert(terrain_world.place(17, 9, grace::Tile::road));
    const int before_woods = terrain_world.snapshot().treasury;
    assert(terrain_world.place(17, 10, grace::Tile::road));
    assert(terrain_world.snapshot().treasury == before_woods - 15);
    assert(terrain_world.terrain(17, 10) == grace::Terrain::grass);

    grace::World staffed;
    assert(staffed.place(11, 9, grace::Tile::pantry));
    assert(staffed.place(13, 7, grace::Tile::chapel));
    assert(staffed.snapshot().active_chapels == 2);
    assert(staffed.snapshot().active_pantries == 0);
    assert(!staffed.home_has_service(10, 7, grace::Tile::pantry));
    assert(staffed.coverage_change_if_placed(grace::Tile::pantry, 10, 9) == 0);
    assert(!staffed.would_serve_home(grace::Tile::pantry, 10, 9, 10, 7));
    staffed.set_care_priority(true);
    assert(staffed.snapshot().active_chapels == 1);
    assert(staffed.snapshot().active_pantries == 1);
    assert(staffed.home_has_service(10, 7, grace::Tile::pantry));
    grace::World shifted_staff;
    assert(shifted_staff.place(9, 7, grace::Tile::home));
    assert(shifted_staff.place(11, 9, grace::Tile::pantry));
    shifted_staff.set_care_priority(true);
    assert(shifted_staff.coverage_change_if_placed(grace::Tile::pantry, 15, 7) == -1);

    grace::World without_care;
    grace::World with_care;
    assert(with_care.place(11, 9, grace::Tile::pantry));
    for (int week = 0; week < 4; ++week) {
        without_care.advance_week();
        with_care.advance_week();
    }
    assert(with_care.household_needs(10, 7).care >
           without_care.household_needs(10, 7).care);
    assert(with_care.snapshot().care > without_care.snapshot().care);

    grace::World events;
    assert(events.can_host_event() == grace::EventError::no_people);
    events.advance_week();
    assert(events.can_host_event() == grace::EventError::none);
    const int before_event = events.snapshot().treasury;
    assert(events.host_event() == grace::EventError::none);
    assert(events.snapshot().treasury == before_event - 30);
    assert(events.snapshot().event_weeks_left == 2);
    assert(events.snapshot().event_cooldown == 6);
    assert(events.host_event() == grace::EventError::cooldown);
    events.advance_week();
    assert(events.snapshot().event_weeks_left == 1);
    assert(events.snapshot().event_cooldown == 5);
    events.advance_week();
    assert(events.snapshot().event_weeks_left == 0);
    grace::World food_drives;
    grace::World no_events;
    for (int week = 0; week < 24; ++week) {
        if (food_drives.can_host_event(grace::EventKind::food_drive) ==
            grace::EventError::none)
            assert(food_drives.host_event(grace::EventKind::food_drive) ==
                   grace::EventError::none);
        food_drives.advance_week();
        no_events.advance_week();
    }
    assert(food_drives.snapshot().events_hosted >= 3);
    assert(food_drives.snapshot().care > no_events.snapshot().care);
    assert(food_drives.snapshot().care >= 50);
    grace::World busy_services;
    assert(busy_services.place(11, 9, grace::Tile::pantry));
    busy_services.advance_week();
    assert(busy_services.can_host_event() == grace::EventError::insufficient_volunteers);
    assert(busy_services.place(9, 7, grace::Tile::home));
    for (int week = 0; week < 3; ++week) busy_services.advance_week();
    assert(busy_services.can_host_event() == grace::EventError::none);
    std::cout << "world tests passed\n";
}
