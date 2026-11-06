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

The current prototype implements a deliberately small version of this loop. Homes need a road connected to the starting chapel; staffed chapels and pantries within five tiles serve them. Selecting either service shows its current and proposed coverage. Home care and belonging change each week. Parks improve both; gatherings focus on belonging and food drives on care. People move into available homes over time, while donations and upkeep change the treasury. Breaking a road route disconnects homes and services until it is repaired. Woods cost extra to clear and the pond shapes expansion.

## Proposed systems

| System | Player decision | Interesting consequence |
| --- | --- | --- |
| Roads and paths | Connect and shape neighborhoods | Access competes with green space and money |
| Homes | Welcome new households | Population increases demand for care and gathering space |
| Chapels and halls | Choose location and capacity | Events create social ties but require volunteers and upkeep |
| Pantry and care center | Cover neighborhoods with support | Helps vulnerable residents during shortages or storms |
| Parks and gardens | Preserve quiet shared space | Raises wellbeing and can soften density |
| Budget | Balance donations, grants, and upkeep | Expanding too quickly can strain the town |
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

Desktop single player, mouse and keyboard, one fixed map, no combat, no online services. The first slice favors a fast build and clear feedback over realistic economics. Numbers and rules are temporary tuning values. Save/load, detailed terrain, road travel distances, seasonal events, and a full UI belong to later milestones.
