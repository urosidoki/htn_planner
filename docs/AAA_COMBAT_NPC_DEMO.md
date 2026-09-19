# AAACombatNPC demo domain

`Domains/AAACombatNPC.domain` is a six-file example for the HTNDemo domain
runner. Its top-level method is `run`. Each plan is one decision tick: a game
host applies the primitive actions, updates facts, and requests a new plan.
The primitives intentionally describe game actions; the HTNDemo domain runner
displays the plan but does not simulate movement, damage, ammo consumption or
order completion.

Included methods use `?inp_` parameters. The compiler validates method signatures,
axiom signatures and direct calls before generating code.

Select **AAACombatNPC** in HTNDemo, then select any world state whose filename
begins `AAACombatNPC_` and run the selected top-level method.

Priority from highest to lowest:

1. High scripted order
2. Visible hostile combat
3. Medium scripted order
4. Investigation of a suspicious stimulus (before combat)
5. Search for a remembered opponent lost from sight (after combat)
6. Low scripted order
7. Idle

Combat chooses the closest visible hostile from `enemy_distance` facts. A
distance below 8 chooses melee; 8 or greater chooses ranged. Branches cover
critical retreat, parry, heavy and quick attacks, weapon switching, cover,
flanking, precision fire, reload, approach and defensive fallback. Investigation
handles radio reports, gunshots, tracks, anomalies and blocked routes. Search
handles tracks, escape sounds, squad routes, cover and the last known position.
Idle covers healing, resupply, weapon repair, patrol, social actions, guard duty
and rest. Scripted orders support evacuate, hold, escort, guard, interact and move.

Worldstates:

| File suffix | Expected first state | Main check |
| --- | --- | --- |
| `high_order` | high_order | Evacuation wins over visible combat |
| `melee` | combat | Enemy 202 at distance 3 wins over enemy 201 at 17 |
| `ranged` | combat | Enemy 302 at distance 16 wins over enemy 301 at 42; switch weapon |
| `medium_order` | medium_order | Escort wins over investigation and search |
| `investigation` | investigation | Suspicious gunshot before any combat memory |
| `search` | search | Lost enemy memory takes precedence over a new anomaly |
| `low_order` | low_order | Guard order wins over patrol idle |
| `idle` | idle | Patrol branch |

To add an order, write `scripted_order <npc> <high|medium|low> <order> <target>`
in a worldstate. Keep one order per priority for deterministic selection. To
simulate state transitions, change the worldstate facts between runs: add or
remove `visible_enemy`, set `combat_memory`, and clear it when search ends.
