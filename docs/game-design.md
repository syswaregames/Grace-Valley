# Game design: Grace Valley

## Promise and tone

A SimCity style town builder where the interesting question is: **what does a thriving, caring community need, and how do we make room for it?** The valley begins with one chapel, one home, and a short road. Players build a place that welcomes neighbors and survives changing seasons.

The fictional setting draws on the social life of church communities: gatherings, volunteer work, food support, music, festivals, and stewardship. Residents have individual needs and preferences. The game treats faith and people with respect; it does not award points for conversion or reduce people to donation generators. Difficulty comes from geography, budgets, capacity, and competing good priorities.

## Core loop

1. Read the town: people, unmet needs, roads, funds, and neighborhood stories.
2. Plan a small improvement: access, homes, a service, public space, or a gathering.
3. Build and watch several weeks pass.
4. See who benefited, what it cost, and what new need appeared.
5. Revise the plan and gradually form distinct neighborhoods.

The current prototype implements a deliberately small version of this loop. New towns begin in Setup, with the weekly clock stopped until the player presses Play. Players mark residential, commercial, and industrial zones; connected plots develop when demand and spare power are high enough. Road-connected power plants supply homes and businesses; outages halt zone development and disable capacity, jobs, and taxes. Shops and workshops provide jobs, tax revenue, and new housing demand. Occupied homes send commuters to businesses over the connected road network. Cars make the flow visible, overloaded roads lower happiness, and paid road upgrades increase local capacity. The City Score combines joy, parks, service coverage, access, power, traffic, and cashflow. It influences all three zone demands, so congestion can slow growth even when jobs are available. Homes need a road connected to the starting chapel; staffed chapels and pantries within five tiles serve them. Selecting either service shows its current and proposed coverage. Home care and belonging change each week. Parks improve both; gatherings focus on belonging and food drives on care. People move into available homes over time. The player sets voluntary giving, service spending, and business tax; these choices affect weekly cashflow, wellbeing, and growth. A negative treasury after a week ends the town in bankruptcy. Breaking a road route disconnects homes and services until it is repaired. Woods cost extra to clear; players can build costly road bridges across the pond to open the far bank.

## Proposed systems

| System | Player decision | Interesting consequence |
| --- | --- | --- |
| Roads, bridges, and traffic | Connect neighborhoods and cross water; upgrade overloaded links | Work commutes crowd shared corridors and affect happiness |
| Power plants | Place enough connected supply for homes and businesses | Outages stop growth; extra capacity adds upkeep |
| Residential zones | Welcome new households | Jobs, parks, and wellbeing drive demand for homes |
| Commercial zones | Make room for shops | Jobs and tax revenue grow with the resident base |
| Industrial zones | Make room for workshops | More jobs and tax revenue, with a care and happiness tradeoff |
| Chapels and halls | Choose location and capacity | Events create social ties but require volunteers and upkeep |
| Pantry and care center | Cover neighborhoods with support | Helps vulnerable residents during shortages or storms |
| Parks and gardens | Preserve quiet shared space | Raises wellbeing and can soften density |
| Budget | Balance giving, business taxes, grants, and upkeep | Tax policy changes revenue and business growth |
| Volunteer time | Assign limited help to events or services | Players choose what receives attention this season |
| Seasons and events | Prepare for harvest, winter, festivals | Temporary needs make static layouts evolve |
| Resident stories | Respond to specific households | Gives city scale decisions a human face |

## Signature ideas

- **Sundays and weekdays:** gathering buildings are busy on weekends; pantries, gardens, schools, and care programs keep the town alive during the week.
- **The fellowship network:** neighboring buildings can share volunteers and supplies. A kitchen near a pantry becomes more useful than either alone.
- **Gentle dilemmas:** a festival can lift spirits, but may delay repair work. A larger sanctuary can serve more people, but a smaller local hall may make a distant neighborhood feel seen.
- **A living valley:** weather, terrain, water, and seasons change which routes and land uses make sense.
- **Multiple ways to flourish:** success is measured by belonging, care coverage, resilience, and the beauty of a place people want to inhabit. No single building type is mandatory everywhere.

## Visual and audio direction

An inviting isometric 2.5D town with readable silhouettes, warm windows, gardens, and seasonal color. Buildings should visibly change as they improve; people and small delivery carts make services legible. The prototype uses a top down grid and flat colors to establish rules first. Later art should support zooming and clear overlays for access and services. Music can blend acoustic instruments and ambient town sounds without imitating a specific worship tradition.

## First playable boundaries

Desktop single player, mouse and keyboard, one fixed map, no combat, no online services. The first slice favors a fast build and clear feedback over realistic economics. Numbers and rules are temporary tuning values. A versioned, manual single-slot save is available; autosave and multiple slots remain future work. Detailed terrain, richer route choices, seasonal events, and a full UI belong to later milestones.
