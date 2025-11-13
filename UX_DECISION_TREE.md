# Loading/Streaming UX Decision Tree

Quick reference to answer your specific design questions.

---

## Question 1: What Should Initial Loading Screen Show?

```
                    ┌─────────────────────────────┐
                    │  LOADING SCREEN DESIGN      │
                    │  (0-2 seconds)              │
                    └─────────────────────────────┘
                                 │
                ┌────────────────┼────────────────┐
                │                │                │
          [What?]          [How?]             [Why?]
            │                 │                 │
    ┌─────────────────┐  ┌──────────────┐  ┌──────────┐
    │ Show:           │  │ Progress by: │  │ Players  │
    │                 │  │              │  │ expect:  │
    │ 1. Progress bar │  │ ✓ Chunk #    │  │          │
    │ 2. Chunk count  │  │ ✓ % complete │  │ 1. Fast  │
    │ 3. Stage name   │  │ ✓ Time left  │  │ 2. Clear │
    │ 4. Time remain  │  │ ✗ Generic %s │  │ 3. Real  │
    │                 │  │              │  │          │
    └─────────────────┘  └──────────────┘  └──────────┘


    GOOD LOADING MESSAGE:
    ┌──────────────────────────────────┐
    │  Generating spawn area...        │
    │  ████████░░░░░░░░░░░░  50%       │
    │  Chunks loaded: 5/9              │
    │  Time remaining: ~1.2s           │
    └──────────────────────────────────┘


    BAD LOADING MESSAGE:
    ┌──────────────────────────────────┐
    │  Generating world...             │
    │  ████░░░░░░░░░░░░░░  10%         │
    │  (player doesn't know what 10% means)
    └──────────────────────────────────┘
```

**Decision Matrix:**

| Metric | Show | Why |
|--------|------|-----|
| **Progress Bar** | YES | Visual progress feels better |
| **Chunk Count** | YES | "5/9" is more real than "50%" |
| **Time Remaining** | YES | "~1.2s" shows it's not slow |
| **Stage Name** | YES | "Spawn area" explains what's loading |
| **Full World Size** | NO | Confuses players ("Why is 5/576?") |
| **Animated Dots** | YES | Shows responsiveness (you do this!) |
| **Loading Tips** | NO | Too many words, slows reading |

---

## Question 2: During Gameplay, How to Show Streaming?

```
                 ┌────────────────────────┐
                 │ STREAMING FEEDBACK     │
                 │ (During Gameplay)      │
                 └────────────────────────┘
                           │
              ┌────────────┴───────────┐
              │                        │
         [Normal Case]          [Problem Case]
              │                        │
         No generation            Generation stalls
         lag detected             >200ms behind
              │                        │
              ▼                        ▼
         ┌──────────────┐        ┌────────────────┐
         │ SILENCE      │        │ SHOW INDICATOR │
         │              │        │                │
         │ Don't show   │        │ Top-right:     │
         │ anything!    │        │ "Generating... │
         │              │        │                │
         │ Why: Fog     │        │ Why: Tell      │
         │ hides chunks │        │ player why     │
         │ Players      │        │ frame is slow  │
         │ expect it    │        │                │
         └──────────────┘        └────────────────┘
             (95%+ of time)        (rare edge case)


    GOOD APPROACH: Silent (Recommended)
    ═══════════════════════════════════════
    During normal gameplay:
    - No UI element shows
    - Fog naturally hides generation
    - Chunks appear smooth at boundaries
    - Players never think "loading"


    FALLBACK: Show When Needed
    ═════════════════════════════════════════
    ┌─────────────────────────────────────────────┐
    │ Game View                                   │
    │ [=============  ...     ][terrain]          │
    │                          ⚙                  │
    │ (When generation is slow)  └─ Subtle "Generating..."
    │                             Fades in/out
    │                             Only if delayed >100ms
    └─────────────────────────────────────────────┘


    DON'T DO THIS: Always Show
    ═════════════════════════════════════════
    ┌─────────────────────────────────────────────┐
    │ Game View                                   │
    │ ⚙ Generating terrain...                     │ ← Annoying!
    │ [=====        ...     ][terrain]            │
    │                                             │
    │ Shows 90% of the time, breaks immersion    │
    └─────────────────────────────────────────────┘
```

**Decision Logic:**

```
if (generation_lag < 100ms) {
    // Normal - no indicator needed
    show_nothing();
} else if (generation_lag < 500ms) {
    // Caution - subtle indicator
    show_indicator("Generating...", opacity=0.5);
} else {
    // Serious lag - full warning
    show_indicator("World is loading...", opacity=1.0);
    reduce_player_speed();  // Optional: slow player
}
```

**Appearance Rules for Indicator:**

```
┌─────────────────────────────────────────┐
│ Style Guidelines:                       │
│                                         │
│ Position:  Top-right corner             │
│ Font:      Small, gray                  │
│ Opacity:   50% when showing             │
│ Text:      "⚙ Generating terrain..."   │
│                                         │
│ Fade-in:   100ms smoothly               │
│ Fade-out:  200ms when caught up         │
│            (no sudden pop-in)           │
│                                         │
│ Duration:  Only while lagging           │
│            Auto-hide when caught up     │
│                                         │
│ Animation: Can be static or             │
│            slowly pulsing (optional)    │
└─────────────────────────────────────────┘
```

---

## Question 3: What if Player Moves Faster Than Chunks Generate?

```
         PLAYER SPEED VS GENERATION SPEED
         ════════════════════════════════════════

         Player position: [====████████████████] Render distance (80 units)
         Chunks ahead:    [✓✓✓✓???????][ungenerated]
                           ready pending
                           ↑     ↑
                      Can move  Problem area


         THREE-TIER RESPONSE SYSTEM
         ════════════════════════════════════════

         ┌─────────────────────────────────────────┐
         │ Tier 1: NORMAL (Distance > 2 chunks)    │
         └─────────────────────────────────────────┘
             Player ahead of generation
             Status: ✓ Chunk 1
                    ✓ Chunk 2
                    ✓ Chunk 3
                    ? Chunk 4-5 (pre-generating)

             Action: Silent, no feedback
             Impact: Zero - player doesn't notice


         ┌─────────────────────────────────────────┐
         │ Tier 2: CAUTION (Distance = 1 chunk)    │
         └─────────────────────────────────────────┘
             Player close to edge
             Status: ✓ Chunk 1
                    ? Chunk 2 (generating)
                    ✗ Chunk 3 (not started)

             Action: Show subtle indicator
                    Bump priority queue
                    Pre-gen ahead of player
             Impact: Small - optional visual


         ┌─────────────────────────────────────────┐
         │ Tier 3: BLOCKED (No terrain exists)     │
         └─────────────────────────────────────────┘
             Player in ungenerated space
             Status: ✗ Chunk 1 (blank void)
                    ✗ Chunk 2 (no data)
                    ? Chunk 3 (pre-generating)

             Action: Show placeholder terrain
                    Let player continue moving
                    Replace placeholder when real chunk ready
             Impact: Medium - visible but acceptable


    WHAT TO NEVER DO:
    ═══════════════════════════════════════════════
    ✗ Hard-block player ("You can't go there")
    ✗ Teleport player back
    ✗ Freeze camera mid-movement
    ✗ Force sudden slowdown


    RECOMMENDED FALLBACK CHAIN:
    ═══════════════════════════════════════════════
    1. Generate chunks ahead (primary prevention)
    2. Pre-generate in player direction (if moving)
    3. Show placeholder terrain (if still too fast)
    4. Allow movement (never block player)
    5. Replace with real terrain when ready (silent swap)
```

**Implementation Priority:**

```
Priority 1: Generate spawn area fast (0.5s)
            ✓ Prevents Tier 3 at start

Priority 2: Pre-gen in player direction
            ✓ Prevents Tier 2-3 while moving

Priority 3: Show indicator in Tier 2
            ✓ Inform players what's happening

Priority 4: Placeholder terrain for Tier 3
            ✓ Rarely needed, safety net only
```

---

## Question 4: Loading Stage Breakdown

```
              YOUR PROPOSED STAGES
              ═══════════════════════════════════════

    Stage 1: Initialize biome system     [5%]
    Stage 2: Generate world heightmap    [20%]   ← Too early!
    Stage 3: Generate spawn chunks       [50%]
    Stage 4: Spawn player                [100%]

    PROBLEM: Stage 2 generates heightmap for entire world
             This defeats the purpose of streaming!


              RECOMMENDED STAGES
              ═══════════════════════════════════════

    ┌──────────────────────────────────────────────┐
    │ PHASE 1: Initialization (0.3s, 0-30%)        │
    └──────────────────────────────────────────────┘
        [████░░░░░░░░░░░░░░░░░░░░░░░░░]

        5%   Load block registry & textures
        10%  Load biome registry & definitions
        20%  Bind renderer (texture atlas)
        30%  Ready to generate

        What to load:
        ✓ Block definitions (which blocks exist)
        ✓ Biome definitions (terrain rules)
        ✓ Textures (for GPU)

        What NOT to load:
        ✗ Any terrain data
        ✗ World heightmap
        ✗ Chunks


    ┌──────────────────────────────────────────────┐
    │ PHASE 2: Spawn Area (0.5s, 30-75%)           │
    └──────────────────────────────────────────────┘
        [████████████████░░░░░░░░░░░░░]

        35%  Generate spawn location (3x3 terrain)
        50%  Generate spawn meshes
        75%  Create GPU buffers for spawn

        What to load:
        ✓ 9 chunks of terrain (3x3 around origin)
        ✓ Meshes for those 9 chunks
        ✓ GPU buffers (Vulkan memory)

        What NOT to load:
        ✗ Remaining 567 chunks
        ✗ Any non-spawn meshes yet
        ✗ Distant chunk GPU buffers


    ┌──────────────────────────────────────────────┐
    │ PHASE 3: Spawn Player (0.2s, 75-95%)         │
    └──────────────────────────────────────────────┘
        [█████████████████████░░░░░░░░░]

        80%  Find safe spawn location
        90%  Initialize player physics
        95%  Ready message

        What to do:
        ✓ Search for safe ground
        ✓ Place player
        ✓ Show "Ready!" prompt


    ┌──────────────────────────────────────────────┐
    │ PHASE 4: Show Gameplay (0.1s, 95-100%)       │
    └──────────────────────────────────────────────┘
        [████████████████████████████░░]

        100% "Ready!" → Fade out loading screen

        Result: PLAYER SPAWNS AT ~1.5 SECONDS


    ┌──────────────────────────────────────────────┐
    │ BACKGROUND: Streaming (infinite, ongoing)    │
    └──────────────────────────────────────────────┘
        (No UI, no blocking, no progress bar)

        Parallel thread:
        1. Generate remaining terrain (all chunks)
        2. Generate meshes (all chunks)
        3. Upload to GPU (as player approaches)
        4. Priority: Closest chunks first
        5. Continue until complete


    VISUAL PROGRESSION:
    ═════════════════════════════════════════════════

    T=0.0s  [████░░░░░░░░░░░░░░░░░░░░░░░░░]  30%
            Loading blocks and biomes...

    T=0.3s  [██████████░░░░░░░░░░░░░░░░░░░░░]  50%
            Generating spawn area...

    T=0.8s  [████████████████░░░░░░░░░░░░░░░░]  75%
            Creating GPU buffers...

    T=1.2s  [████████████████████████░░░░░░░░░]  95%
            Finding safe spawn...

    T=1.5s  [█████████████████████████████░░░░]  100%
            Ready!
            [CLICK to start or fade auto]

    T=1.6s  [LOADING SCREEN DISAPPEARS]
            GAMEPLAY BEGINS
            Background: Streaming ~567 chunks

    T=5.0s  Player has moved ~10 blocks
            Background status:
            - Spawn area: ✓ Ready
            - 5x5 area: ✓ Ready
            - 7x7 area: ✓ 2 chunks pending
            - 9x9 area: 10 chunks pending
            Player sees: Normal gameplay, no UI


    COMPARISON TABLE:
    ═════════════════════════════════════════════════

    │ Metric              │ Before  │ After    │
    ├─────────────────────┼─────────┼──────────┤
    │ Stage 1 time        │ 0.3s    │ 0.3s ✓   │
    │ Stage 2 time        │ 20-40s  │ 0.5s ✓   │
    │ Stage 3 time        │ 5-10s   │ 0.2s ✓   │
    │ Total wait          │ 30-60s  │ 1.5s ✓   │
    │ Player can move     │ Never   │ @1.5s ✓  │
    │ Processing after    │ None    │ Silent ✓  │
    │ UI complexity       │ Simple  │ Slightly↑ │
```

---

## Master Decision Summary

### Question: "Is this a good user experience?"

**Answer: YES, with streaming. Your stages are close, but:**

```
✓ DO THIS:
  1. Load registries (0.3s)
  2. Generate spawn area ONLY (0.5s)
  3. Create spawn GPU buffers (0.2s)
  4. Spawn player (0.2s)
  5. Start background thread for rest

✗ DON'T DO THIS:
  1. Load registries (0.3s)
  2. Generate ENTIRE world (20-60s)  ← BLOCKED FOR MINUTES
  3. Decorate entire world (trees)
  4. Create ALL GPU buffers
  5. Finally spawn player


RESULT:
  Before: 30-60 second wait before gameplay
  After:  1.5 second wait before gameplay
```

---

## Flowchart: What to Build

```
                        ┌──────────────┐
                        │ START GAME   │
                        └──────┬───────┘
                               │
                        ┌──────▼───────┐
                        │ Show Loading │
                        │ Screen       │
                        └──────┬───────┘
                               │
                    ┌──────────┴──────────┐
                    │                     │
            ┌───────▼──────┐       ┌─────▼──────┐
            │ Main Thread  │       │ Background │
            │ (Blocking)   │       │ Thread     │
            └───────┬──────┘       │ (Async)    │
                    │              └─────┬──────┘
       ┌────────────┼────────────────┐   │
       │            │                │   │
       ▼            ▼                ▼   ▼
    [Reg]→[Spawn]→[GPU]→[Find]→[SPAWN]  [Stream]
    0.3s  0.5s     0.2s   0.2s  1.5s    Ongoing

    At T=1.5s: Show gameplay, continue streaming
    At T=∞:    All chunks generated
```

---

## Quick Answer Card

**Keep this handy when making decisions:**

```
┌─────────────────────────────────────────────────────┐
│ QUICK UX DECISION CARD                              │
├─────────────────────────────────────────────────────┤
│                                                     │
│ Q: How fast should loading be?                      │
│ A: 1-2 seconds (spawn area only)                   │
│                                                     │
│ Q: What should progress bar show?                   │
│ A: Chunks loaded (e.g., "5/9"), not fake %s       │
│                                                     │
│ Q: Streaming UI during gameplay?                    │
│ A: NONE (silence is golden)                        │
│    Exception: Show only if stalling >200ms         │
│                                                     │
│ Q: What if player outpaces generation?             │
│ A: Never block. Show placeholder, then replace.    │
│                                                     │
│ Q: How many stages?                                │
│ A: 5 (Init → Spawn → GPU → Find → Ready)           │
│    + Background (streaming)                         │
│                                                     │
│ Q: Total load time target?                          │
│ A: 1.5 seconds before gameplay                     │
│                                                     │
│ Q: Is my design good?                               │
│ A: YES! Just move the heavy lifting to             │
│    background thread.                              │
│                                                     │
└─────────────────────────────────────────────────────┘
```

