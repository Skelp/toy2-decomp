# Toy Story 2: Buzz Lightyear to the Rescue! — Gameplay Context

## Scope

This document describes the PC game represented by `toy2.exe`.

It covers campaign flow, player abilities, objectives, collectibles, gadgets, enemies, bosses, hazards, and level structure.

## Game overview

The game is a third-person platform adventure with combat, collection, races, puzzles, and boss fights.

The player controls Buzz Lightyear. Buzz explores large levels based on locations from *Toy Story 2*.

The campaign has 15 levels in five zones. Each zone has two exploration levels and one boss level.

Each exploration level contains five Pizza Planet Tokens. The ten exploration levels contain 50 tokens in total.

Boss levels open when the player has enough tokens. The player can leave an exploration level after one token.

Later boss gates require more tokens. The player must complete optional objectives and revisit some earlier levels.

## Campaign structure

| Zone | Exploration level 1 | Exploration level 2 | Boss level | Required tokens |
|---|---|---|---|---:|
| 1 | Andy's House | Andy's Neighborhood | Bombs Away! | 3 |
| 2 | Construction Yard | Alleys and Gullies | Slime Time | 10 |
| 3 | Al's Toy Barn | Al's Space Land | Toy Barn Encounter | 18 |
| 4 | Elevator Hop | Al's Penthouse | The Evil Emperor Zurg | 28 |
| 5 | Airport Infiltration | Tarmac Trouble | Final Showdown | 40 |

A boss victory opens the next zone. The final boss needs 40 of the 50 available tokens.

The game does not require full completion. Full completion needs all five tokens from each exploration level.

## Exploration-level flow

An exploration level is an open objective area. Several objectives can remain active at the same time.

Most exploration levels use five common objective types:

| Objective type | Common form |
|---|---|
| Five-item quest | Collect five related objects for a character. |
| Timed challenge | Complete a route, race, or collection task before time expires. |
| Combat challenge | Defeat a mini-boss or a special enemy. |
| Environment challenge | Use switches, moving objects, targets, or traversal devices. |
| Coin challenge | Collect 50 coins and return to Hamm. |

The player can collect a token and continue the level. Token collection does not force an exit.

The pause menu also provides an Exit Level command. This command returns the player to the world map.

Most unfinished local activity resets after the player reloads the level. Collected tokens remain collected.

## Progress and persistent unlocks

### Pizza Planet Tokens

Pizza Planet Tokens control campaign progress. Each token belongs to one specific level objective.

The world map uses the total token count for boss gates. A collected token remains collected across later visits.

### Boss progress

A defeated boss opens the next zone. Boss fights do not contain the five-objective structure of exploration levels.

### Mr. Potato Head gadgets

Mr. Potato Head appears in five exploration levels. Buzz must find one missing body part in each level.

Returning the body part unlocks one gadget. The gadget then becomes available in all levels with matching pickups.

| Body part | Level | Gadget | Main purpose |
|---|---|---|---|
| Ear | Andy's House | Cosmic Shield | Protection and shield routes |
| Eye | Construction Yard | Disk Launcher | Heavy enemy and boss damage |
| Arm | Al's Toy Barn | Rocket Boots | Fast horizontal travel and races |
| Foot | Elevator Hop | Grappling Hook | Travel to marked grapple points |
| Mouth | Airport Infiltration | Hover Boots | Cross hazards and follow high terrain |

A gadget unlock is permanent. A gadget pickup only gives temporary use or limited ammunition.

Some early levels contain routes that need later gadgets. This design makes level revisits part of normal progress.

## Player movement

### Ground movement

Buzz can walk, turn, jump, attack, push objects, and enter first-person aim.

Some surfaces change movement. Kitchen water can make Buzz skid without causing damage.

Other surfaces cause damage. Green goo and wet concrete are common examples.

### Jump and double jump

Buzz has a normal jump and a wing-assisted second jump.

The second jump starts while Buzz is airborne. It extends height and distance.

### Ledge grab

Buzz can catch selected ledges during a jump or fall. He hangs from the edge and can pull himself up.

A ledge grab can interact with moving platforms. In Alleys and Gullies, it can stop one floating box row.

### Pole and rope climb

Buzz attaches to vertical poles and ropes on contact. He can climb, descend, and rotate around them.

The jump control releases Buzz from the pole. His facing direction affects the release direction.

### Horizontal bars

Buzz attaches to horizontal bars automatically. The bar carries him around one rotation before release.

A well-timed jump increases the release distance. Several routes depend on this extra distance.

### Zip-lines

Buzz can attach to zip-lines and move along a fixed route. He can release early with a jump.

### Pushable objects

A green hand icon marks many pushable objects. Buzz must contact the correct side to push them.

Most pushable objects move along a fixed path. Boxes often form steps or open access to high routes.

### Stomp

Buzz performs a stomp when the player uses the spin control in the air.

The stomp sends Buzz straight down. It activates buttons, springs, pumps, and bounce objects.

Some objects return a stronger bounce after a stomp. The rubber-duck puzzle uses this behavior.

## Combat

### Wrist laser

The wrist laser is Buzz's main ranged attack. Buzz can fire during normal movement or first-person aim.

First-person aim gives more control over distant targets. The game can also help select a nearby target.

Holding the laser control charges a stronger shot. A green-laser pickup gives a limited set of stronger shots.

### Spin attack

The spin is Buzz's main close-range attack. A short input gives a short spin.

Holding the control charges a longer full-power spin. Buzz can jump while this spin remains active.

The spin damages shielded enemies and several bosses. It can also stop or reflect selected projectiles.

### Damage and health

The HUD shows Buzz's health as a battery. Battery pickups restore health.

Enemy contact, projectiles, moving hazards, and dangerous surfaces can reduce health. Hits can also knock Buzz back.

Some hits cancel first-person aim, grapple travel, or another active movement state.

### Lives and death

Buzz loses a life when his health reaches zero or when he falls into a lethal area.

Extra-life pickups add one life. Boss arenas can contain lethal edges or fall zones.

After death, Buzz returns to a level start point or a local restart point.

## Collectibles and pickups

| Item | Effect |
|---|---|
| Pizza Planet Token | Completes one level objective and increases campaign progress. |
| Coin | Adds to the current level's coin total. |
| Battery | Restores health. |
| Extra life | Adds one life. |
| Green laser | Gives a limited number of stronger laser shots. |
| Disk | Adds Disk Launcher ammunition. |
| Gadget pickup | Starts the unlocked gadget for a limited time. |
| Quest item | Adds progress to one five-item objective. |
| Mr. Potato Head part | Enables one permanent gadget unlock. |

### Coin behavior

Hamm awards one token after Buzz collects 50 coins in an exploration level.

Levels contain more than 50 available coins. The player can miss some coins and still complete the objective.

Many enemies drop one coin after their first defeat during the current visit.

The enemy can return after defeat. Later defeats during the same visit do not produce another coin.

Reloading the level restores the first-defeat coin rewards.

## Gadgets

### Cosmic Shield

The Cosmic Shield places Buzz inside a rolling protective sphere.

It blocks many hazards and changes normal movement. Some routes require the shield.

The shield remains active for a limited time. Al's Space Land uses it as an important route tool.

### Disk Launcher

The Disk Launcher fires disks at nearby enemies. It can select a suitable target automatically.

Disk pickups provide ammunition. The launcher works well against shielded enemies.

The Jackhammer boss fight in Construction Yard is designed around this gadget.

### Rocket Boots

Rocket Boots move Buzz quickly across mostly horizontal ground.

They are the intended tool for the RC Car race in Andy's Neighborhood.

They also shorten long routes. They do not provide full flight or free vertical control.

### Grappling Hook

The Grappling Hook works through first-person aim. Valid grapple points show a red target marker.

A successful shot pulls Buzz toward the point. The route can end when Buzz reaches the target or takes damage.

Alleys and Gullies contains routes that expect this gadget after its later unlock.

### Hover Boots

Hover Boots hold Buzz above the surface below him. They do not give free vertical flight.

Buzz rises when the ground below becomes higher. He descends when the supporting terrain becomes lower.

The rings on the boots turn red shortly before the effect ends.

## Recurring characters and objective roles

| Character | Role |
|---|---|
| Rex | Reports the remaining token objectives in an exploration level. |
| Hamm | Awards a token after Buzz collects 50 coins. |
| Mr. Potato Head | Trades a missing body part for a permanent gadget unlock. |
| Bo Peep | Gives the five-sheep objective in Andy's House. |
| Sarge | Gives the five-troop objective in Andy's Neighborhood. |
| Slinky | Gives timed or collection challenges in several levels. |
| RC Car | Gives race challenges in the early levels. |
| Jessie | Gives a five-item objective in Al's Penthouse. |
| Bullseye | Gives the timed horseshoe race in Al's Penthouse. |
| Little Tike characters | Give several five-item objectives in later levels. |

Rex appears in exploration levels and explains unfinished objectives. He does not serve this role in boss levels.

## Enemies and hazards

### Common enemy behavior

Basic enemies patrol a small area, approach Buzz, fire projectiles, or attack on contact.

Many enemies return shortly after defeat. Their return keeps traversal routes active and dangerous.

Shielded enemies resist normal laser fire. Spin attacks and disks can defeat them.

### Common hazards

| Hazard | Typical behavior |
|---|---|
| Green goo | Causes contact damage. |
| Wet concrete | Causes damage and blocks direct travel. |
| Water | Can support platforms or change movement. |
| Fire vent | Activates in a repeating cycle. |
| Electrical bar | Causes damage during contact. |
| Cannon | Fires on a fixed direction or cycle. |
| Drill or saw | Uses a fixed movement or timing pattern. |
| Falling object | Drops after a trigger or repeating delay. |
| Moving vehicle | Damages or pushes Buzz during contact. |
| Lethal fall zone | Removes a life and restarts the encounter. |

### Moving objects

The game uses drawers, elevators, beams, floating boxes, luggage, cranes, and other moving objects.

Most move on fixed routes. A switch, puzzle, timer, or level event can start their movement.

Some devices return to their first position after a delay. The Construction Yard digger scoop is one example.

## Level reference

### 1. Andy's House

**Type:** Exploration level and movement tutorial.

**Token objectives:**

1. Defeat the Tin Robot.
2. Collect Bo Peep's five sheep.
3. Complete the RC Car race.
4. Reach the hidden basement token with movable boxes.
5. Collect 50 coins for Hamm.

The level introduces pushing, double jump, pole climbing, zip-lines, bars, ledge grabs, springs, stomps, and first-person aim.

Mr. Potato Head's Ear unlocks the Cosmic Shield. All five tokens are available during the first visit.

### 2. Andy's Neighborhood

**Type:** Exploration level.

**Token objectives:**

1. Collect Sarge's five troops.
2. Win the RC Car circuit race.
3. Complete the rubber-duck pump and bounce puzzle.
4. Defeat the Zurg Kite.
5. Collect 50 coins for Hamm.

The level uses yards, pool platforms, laundry lines, tree routes, swings, a tire rope, and moving enemies.

One troop appears after Buzz stomps a molehill sequence seven times.

Rocket Boots are the intended race tool. Buzz can also block RC and force repeated rebounds to win early.

### 3. Bombs Away!

**Type:** Boss level.

**Gate:** 3 tokens.

The biplane circles the arena and then starts a direct attack run.

The direct run gives Buzz the safest laser opportunity. A detailed walkthrough reports ten successful hits.

### 4. Construction Yard

**Type:** Exploration level.

**Token objectives:**

1. Collect the Foreman's five Little Tike Workers.
2. Complete the paint-mixing puzzle.
3. Collect Slinky's five wrenches before time expires.
4. Defeat the Jackhammer.
5. Collect 50 coins for Hamm.

**Paint recipes:**

| Colors | Result |
|---|---|
| Red and blue | Purple |
| Red and yellow | Orange |
| Blue and yellow | Green |

A wrong mix can be emptied without a permanent penalty.

Slinky's wrench challenge uses a 50-second limit.

The level uses a digger, crane, drawers, an elevator, moving beams, falling screws, and wet concrete.

Mr. Potato Head's Eye unlocks the Disk Launcher. The Jackhammer fight provides disk ammunition.

### 5. Alleys and Gullies

**Type:** Exploration level.

**Token objectives:**

1. Collect Slinky's five bones on floating debris.
2. Collect Mother Duck's five ducklings.
3. Defeat the Clown Top.
4. Complete the balloon-table traversal route.
5. Collect 50 coins for Hamm.

The level uses water channels, floating boxes, electrical zip-lines, fire escapes, dumpsters, awnings, scales, and balloons.

A shielded robot can be defeated with spin attacks. Some routes expect the Grappling Hook.

Buzz can reach some tokens before that unlock. A ledge grab can stop one moving box row.

### 6. Slime Time

**Type:** Boss level.

**Gate:** 10 tokens.

The slime boss jumps toward Buzz and can fire green projectiles.

The boss is vulnerable between jumps. Sustained laser fire makes it shrink into its can.

The boss later returns from the can and continues the fight.

### 7. Al's Toy Barn

**Type:** Exploration level.

**Token objectives:**

1. Open the office cabinet route by shooting three locks.
2. Collect the Chicken's five chicks.
3. Complete the Rooster's repeated skateboard route.
4. Defeat the Dinosaur.
5. Collect 50 coins for Hamm.

Shooting the cabinet locks extends drawers and opens a high route.

The Rooster route uses a skateboard, trampoline, trolley, boxes, a bar, and a zip-line.

The first route completion gives a chick. A later completion gives the token.

The Dinosaur arena contains damaging goo and small areas of safe floor.

Mr. Potato Head's Arm unlocks Rocket Boots. Full completion also needs the Disk Launcher and Hover Boots.

### 8. Al's Space Land

**Type:** Exploration level.

**Token objectives:**

1. Collect the Mothership's five Aliens.
2. Defeat the Buzz Lightyear Buggy.
3. Complete the Claw Machine objective.
4. Complete the upper saucer or traversal objective.
5. Collect 50 coins for Hamm.

The level uses poles, arcade machines, a ball pit, a moving claw, boxes, lasers, and saucer routes.

Spin attacks damage the Buggy. The spin can also stop its descending rocket.

The Cosmic Shield is the main earlier gadget requirement. All five tokens can be available on the first visit.

### 9. Toy Barn Encounter

**Type:** Boss level.

**Gate:** 18 tokens.

The mothership fight uses several repeated phases.

1. Shoot the central hull.
2. Detach one pod.
3. Defeat the enemy from that pod.
4. Avoid the sweeping laser.
5. Repeat until all pods are gone.
6. Damage the exposed hull.

Some pod enemies carry shields and require spin attacks.

### 10. Elevator Hop

**Type:** Exploration level.

**Known token objectives:**

1. Collect Mother Mouse's five clockwork mice.
2. Solve the wire-position puzzle.
3. Complete the vertical machinery routes.
4. Complete an upper combat or secret objective.
5. Collect 50 coins for Hamm.

The exact public names of some objectives are unclear.

The level uses electrical bars, grapple points, fire vents, air vents, elevator cars, fans, and shaft platforms.

The wire puzzle starts with red and blue high and green low.

1. Move blue up and red down.
2. Move green up and blue down.

The solution opens the gate and starts the elevators.

Mr. Potato Head's Foot unlocks the Grappling Hook. The unlock permits first-visit completion.

### 11. Al's Penthouse

**Type:** Exploration level.

**Token objectives:**

1. Complete Bullseye's timed five-horseshoe race.
2. Collect Jessie's five Critters.
3. Defeat the Gunslinger.
4. Complete the environment puzzle or secret route.
5. Collect 50 coins for Hamm.

The level uses furniture, a fireplace, lamps, cabinets, bathroom routes, and kitchen machinery.

Buttons can disable fixed cannon hazards. No later gadget requirement is known.

### 12. The Evil Emperor Zurg

**Type:** Boss level.

**Gate:** 28 tokens.

Zurg moves above the arena and fires green balls.

A full-power spin lets Buzz jump while the attack remains active.

Spin can stop or return the green balls. A fall from the arena costs one life.

### 13. Airport Infiltration

**Type:** Exploration level.

**Known token objectives:**

1. Collect the Little Tike Pilot's five passengers.
2. Complete the Rocky Gibraltar challenge.
3. Defeat the Prospector.
4. Complete the Broken Plane route.
5. Collect 50 coins for Hamm.

The level uses conveyor belts, bounce suitcases, X-ray machines, plane wings, luggage stacks, cowboys, and snakes.

Mr. Potato Head's Mouth unlocks Hover Boots. The boots open upper luggage and broken-plane routes.

The same gadget also permits full completion of Al's Toy Barn.

### 14. Tarmac Trouble

**Type:** Exploration level.

**Known token objectives:**

1. Collect five Little Tike luggage pieces.
2. Complete Slinky's no-jump slime race.
3. Complete a combat or mini-boss objective.
4. Complete an environment traversal objective.
5. Collect 50 coins for Hamm.

The exact public names of two objectives are unclear.

The level uses open tarmac areas, circling aircraft, slime paths, windsocks, poles, and airport machinery.

Slinky's challenge fails when Buzz jumps or takes damage. Rocket Boots only shorten travel.

### 15. Final Showdown

**Type:** Boss level.

**Gate:** 40 tokens.

The final encounter starts when Buzz approaches Jessie and Woody.

Gunslinger, Blacksmith, and Prospector take part in the fight.

The encounter reuses enemies from earlier battles. Each enemy keeps its main attack style.

## Boss behavior summary

| Boss | Main behavior |
|---|---|
| Tin Robot | Approaches Buzz and trades close attacks. Spin knocks it back. |
| Zurg Kite | Moves through the air and uses ranged attacks. |
| Biplane | Circles the arena and makes direct firing runs. |
| Jackhammer | Chases Buzz around a rooftop. Disks provide the intended damage. |
| Clown Top | Closes distance and attacks through movement or contact. |
| Slime | Jumps, fires projectiles, shrinks into a can, and returns. |
| Dinosaur | Chases Buzz across safe floor islands surrounded by goo. |
| Buzz Lightyear Buggy | Drives near Buzz, launches a rocket, and takes spin damage. |
| Mothership | Releases pod enemies, uses a sweeping laser, and exposes its hull. |
| Gunslinger | Uses ranged attacks and returns during the final encounter. |
| Zurg | Flies, launches green balls, and reacts to full-power spin. |
| Blacksmith | Uses a heavy slam and a ground shockwave. |
| Prospector | Appears in Airport Infiltration and the final encounter. |

## Timed challenges and special rules

| Challenge | Rule |
|---|---|
| RC Car race | Beat RC around the route. Rocket Boots are the intended aid. |
| Construction Yard wrenches | Collect five wrenches within 50 seconds. |
| Rooster route | Complete the route once for a chick and again for a token. |
| Bullseye race | Collect five horseshoes before time expires. |
| Tarmac slime race | Do not jump and do not take damage. |

A failed timed challenge returns to its start state. The player can talk to the character and try again.

## Puzzles and environment rules

### Rubber duck

Buzz uses pumps, stomps, and bounce behavior to move through the rubber-duck objective.

The stomp can sink the duck or activate part of the route.

### Paint mixing

The Construction Yard puzzle uses three required color pairs.

An incorrect mix does not block later attempts. The player can empty the container and start again.

### Cabinet locks

Al's Toy Barn has three target locks. Shooting all three extends drawers and creates a path.

### Elevator wires

Elevator Hop uses three wire positions. The correct two-step sequence starts the elevators.

### Temporary switches

Some switches start a device for a limited time. The device later returns to its first position.

The player must reach the next platform before the timer ends.

## Gameplay quirks and edge cases

| Behavior | Result |
|---|---|
| Buzz blocks RC during the race | RC rebounds and can lose progress. |
| Buzz hangs from a moving box | One box row can stop while other rows continue. |
| An enemy returns after defeat | It gives no second coin during that visit. |
| Buzz makes a wrong paint color | The player can empty it and retry. |
| Buzz spins into selected projectiles | The projectile can stop or return. |
| A hazard hits Buzz during aim or grapple | The active mode can end immediately. |
| Buzz walks through kitchen water | Movement becomes slippery without health loss. |
| A timed machine completes its cycle | The machine can return to its first position. |
| Buzz repeats the Rooster route | The second completion gives a different reward. |

## PC version features

The PC version uses keyboard and controller input. The setup program permits control remapping.

The game uses a separate launcher for display and controller settings. The launcher needs keyboard navigation.

Gameplay does not use the mouse as a normal control device.

The original display design targets a 4:3 image. Modern fixes can add wide-screen support and other compatibility changes.

The PC and PlayStation versions use film-based video scenes. The Nintendo 64 version uses still images and text instead.

## Unclear or incomplete gameplay details

The available material does not give exact values for these items:

- Buzz's maximum health
- Damage from each enemy and hazard
- Laser charge time and damage
- Spin charge time and recovery time
- Gadget duration and ammunition values
- Jump height and movement speed
- Life limits in the original PC release
- Exact restart points after death
- Exact names of some Elevator Hop objectives
- Exact names of some Tarmac Trouble objectives
- The full Prospector attack pattern
- The exact autosave point after token collection
