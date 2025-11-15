# Biome Blending - Visual Diagrams
**Agent 10 - Biome Blending Research Team**

This document provides ASCII diagrams and visual representations of the biome blending system.

---

## Diagram 1: Temperature-Moisture Biome Space

```
Moisture
100 │
    │  ╔═══════════════╗
 90 │  ║ RAINFOREST    ║
    │  ╚═══════════════╝
 80 │              ╔═══════════════╗
    │              ║ SWAMP         ║
 70 │  ╔═══════╗  ╚═══════════════╝
    │  ║WINTER ║      ╔═══════════════╗
 60 │  ║FOREST ║      ║ FOREST        ║
    │  ╚═══════╝      ╚═══════════════╝
 50 │                     ╔═══════════════╗
    │                     ║ PLAINS        ║
 40 │  ╔═══════════════╗  ╚═══════════════╝
    │  ║ TUNDRA        ║      ╔═══════════════╗
 30 │  ╚═══════════════╝      ║ MOUNTAIN      ║
    │                          ╚═══════════════╝
 20 │
    │
 10 │                              ╔═══════════════╗
    │                              ║ DESERT        ║
  0 │                              ╚═══════════════╝
    └───────────────────────────────────────────────── Temperature
    0   10  20  30  40  50  60  70  80  90  100
```

**Blending Zones:** Represented by overlapping boxes
- Overlapping edges = transition zones
- Distance between centers = blend strength

---

## Diagram 2: Influence Falloff Curve

```
Influence Weight
1.0 │ ██████████
    │ ██████████
    │ ██████████   ← Biome center (100% influence)
    │ ██████████
0.8 │ ██████████
    │ ██████████░░
    │ █████████░░░  ← Blend distance starts (15 units)
0.6 │ ████████░░░░
    │ ███████░░░░░
    │ ██████░░░░░░
0.4 │ █████░░░░░░·
    │ ████░░░░░··
    │ ███░░░░···
0.2 │ ██░░░······  ← Exponential falloff
    │ █░░·······
    │ ░·········
0.0 │ ··········   ← Search radius ends (25 units)
    └────────────────────────────────────
    0   5   10  15  20  25  30  35  40

Legend:
█ = Full influence (inner zone, linear falloff)
░ = Blending zone (exponential decay)
· = No influence (outside search radius)
```

**Key Points:**
- 0-15 units: Linear falloff from 1.0 to ~0.4
- 15-25 units: Smooth exponential decay to 0.0
- >25 units: Zero influence

---

## Diagram 3: Multi-Biome Blending Example

### Scenario: Player standing where Desert, Forest, and Plains meet

```
                    FOREST
                    (T=55, M=70)
                        │
                        │
        ┌───────────────┼───────────────┐
        │               │               │
        │   ░░░░░░░    ▓▓▓    ░░░░░░░   │
        │  ░░░░░░░░   ▓▓▓▓▓   ░░░░░░░░  │
        │ ░░░░░░░░░  ▓▓▓███▓▓▓  ░░░░░░░░░│
PLAINS──┤░░░░░░░░░░ ▓▓███████▓▓ ░░░░░░░░░├──DESERT
(T=50)  │░░░░░░░░░░▓▓▓███P███▓▓▓░░░░░░░░░│  (T=90)
(M=45)  │ ░░░░░░░░░▓▓▓███████▓▓▓░░░░░░░░ │  (M=5)
        │  ░░░░░░░░ ▓▓▓█████▓▓▓ ░░░░░░░  │
        │   ░░░░░░░  ▓▓▓▓▓▓▓▓▓  ░░░░░░   │
        │    ░░░░░░   ▓▓▓▓▓▓▓   ░░░░░    │
        └─────────────────────────────────┘

Legend:
███ = Dominant biome (>60% influence)
▓▓▓ = Heavy blending (30-60% influence)
░░░ = Light blending (10-30% influence)
P   = Player position

At point P:
- Forest influence:  0.45 (45%)
- Plains influence:  0.35 (35%)
- Desert influence:  0.20 (20%)
```

**Terrain at P:**
- Surface block: 45% grass, 35% grass, 20% sand → likely grass
- Tree density: 0.45×80% + 0.35×20% + 0.20×0% = 43%
- Terrain height: blended average of all three

---

## Diagram 4: Transition Zone Cross-Section

### Desert → Forest Transition (Side View)

```
Height (Y)
120 │
    │
110 │                        ╱╲    ╱╲
    │                       ╱  ╲  ╱  ╲
100 │                      ╱    ╲╱    ╲
    │                     ╱            ╲
 90 │                    ╱              ╲
    │                   ╱                ╲
 80 │    ┌─────────────────────────────────┐
    │    │░░░░░░░░░░░TRANSITION░░░░░░░░░░░░│
 70 │────┴─────────────────────────────────┴────
    │████████████████████████████████████████████
 60 │████████████████████████████████████████████
    │█▓▓▓▓▓▓░░░░░░░░····                 ████████
 50 │▓▓▓▓▓▓▓░░░░░░░····                  ████████
    0   100  200  300  400  500  600  700  800
    └───────────────────────────────────────────── Distance (blocks)

Legend:
█ = Stone/underground
▓ = Sand (desert surface)
░ = Mixed sand/grass
· = Grass (forest surface)
╱╲= Trees (increasing density)

Terrain height:
▂▂▂▂▃▃▄▄▅▅▆▆▇▇██  ← Gradual rise from desert to forest
```

**Profile breakdown:**
- 0-100 blocks: Pure desert (flat, sand)
- 100-200 blocks: Early transition (sand dominates, occasional grass patches)
- 200-400 blocks: Mid transition (mixed sand/grass, terrain rising)
- 400-600 blocks: Late transition (grass dominates, scattered sand)
- 600-800 blocks: Pure forest (full tree density, grass)

---

## Diagram 5: Algorithm Flow Diagram

```
┌─────────────────────────────────────────┐
│  Chunk Generation Starts                │
│  For each column (x, z):                │
└────────────┬────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────┐
│  STEP 1: Get Biome Influences           │
│  ┌───────────────────────────────┐      │
│  │ Sample temp/moisture noise    │      │
│  │ Find nearby biomes            │      │
│  │ Calculate weighted influences │      │
│  │ Normalize to sum = 1.0        │      │
│  └───────────────────────────────┘      │
└────────────┬────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────┐
│  STEP 2: Blend Terrain Height           │
│  ┌───────────────────────────────┐      │
│  │ For each influenced biome:    │      │
│  │   - Calculate height params   │      │
│  │   - Sample terrain noise      │      │
│  │   - Weight by influence       │      │
│  │ Sum weighted heights          │      │
│  └───────────────────────────────┘      │
└────────────┬────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────┐
│  STEP 3: Fill Column (for each Y)       │
│  ┌───────────────────────────────┐      │
│  │ If Y < terrain height:        │      │
│  │   Determine block type        │      │
│  │   (use blended selection)     │      │
│  │ Else:                          │      │
│  │   Air or water                │      │
│  └───────────────────────────────┘      │
└────────────┬────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────┐
│  STEP 4: Post-Processing                │
│  ┌───────────────────────────────┐      │
│  │ Generate features (trees)     │      │
│  │ Apply blended tree density    │      │
│  │ Place vegetation              │      │
│  └───────────────────────────────┘      │
└────────────┬────────────────────────────┘
             │
             ▼
         [ Complete ]
```

---

## Diagram 6: Cache Resolution Visualization

### 8-Block Influence Cache Resolution

```
Chunk (32×32 blocks)
┌─────────┬─────────┬─────────┬─────────┐
│ Cache   │ Cache   │ Cache   │ Cache   │
│ Cell A  │ Cell B  │ Cell C  │ Cell D  │
│ (8×8)   │ (8×8)   │ (8×8)   │ (8×8)   │
├─────────┼─────────┼─────────┼─────────┤
│ Cache   │ Cache   │ Cache   │ Cache   │
│ Cell E  │ Cell F  │ Cell G  │ Cell H  │
│ (8×8)   │ (8×8)   │ (8×8)   │ (8×8)   │
├─────────┼─────────┼─────────┼─────────┤
│ Cache   │ Cache   │ Cache   │ Cache   │
│ Cell I  │ Cell J  │ Cell K  │ Cell L  │
│ (8×8)   │ (8×8)   │ (8×8)   │ (8×8)   │
├─────────┼─────────┼─────────┼─────────┤
│ Cache   │ Cache   │ Cache   │ Cache   │
│ Cell M  │ Cell N  │ Cell O  │ Cell P  │
│ (8×8)   │ (8×8)   │ (8×8)   │ (8×8)   │
└─────────┴─────────┴─────────┴─────────┘

Cache entries per chunk: 4×4 = 16
Total blocks per chunk: 32×32 = 1024
Compression ratio: 64:1
```

**Trade-off:**
- Coarser cache = less memory, faster lookups
- Finer cache = more detail, higher memory cost
- 8-block resolution = sweet spot for biome blending

---

## Diagram 7: Block Selection Weighted Pool

### Example: Mixed Desert/Forest Zone

```
Biome Influences:
┌──────────┬─────────┬─────────┐
│ Desert   │ Forest  │ Plains  │
│ 50%      │ 35%     │ 15%     │
└──────────┴─────────┴─────────┘
     │          │         │
     ▼          ▼         ▼
Block Contributions:
┌──────────┬─────────┬─────────┐
│ Sand (4) │ Grass(3)│ Grass(3)│
│ 50%      │ 35%     │ 15%     │
└──────────┴─────────┴─────────┘
     │          │         │
     └──────────┴────┬────┘
                     ▼
           Weighted Pool:
     ┌─────────────────────┐
     │ Sand (4):  50%      │
     │ Grass (3): 50%      │
     └─────────────────────┘
                │
                ▼
        Random Selection:
     [0.0 ───── 0.5 ───── 1.0]
      └─Sand─┘  └─Grass──┘

     Roll: 0.42 → Sand
     Roll: 0.68 → Grass
     Roll: 0.51 → Grass
```

**Result:** Natural 50/50 mix of sand and grass blocks

---

## Diagram 8: Mountain-Ocean Extreme Transition

### Handling Large Height Differences

```
Height
150 │         ▲
    │        ╱█╲
140 │       ╱███╲
    │      ╱█████╲     MOUNTAIN
130 │     ╱███████╲    (height mult: 3.5)
    │    ╱█████████╲
120 │   ╱███████████╲
    │  ╱█████████████╲
110 │ ╱███████████████╲
    │╱█████████████████╲
100 │███████████████████╲
    │████████████████████╲
 90 │█████████████████████╲
    │██████████████████████╲
 80 │███████████████████████╲
    │████████████████████████▓
 70 │█████████████████████████▓
    │██████████████████████████▓░
 60 │███████████████████████████▓░·  OCEAN (height: ~50)
    │████████████████████████████▓░·
 50 │█████████████████████████████▓░·
    │█▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▓░·
    0   100  200  300  400  500  600  700  800  900 1000
    └────────────────────────────────────────────────────
                    Distance (blocks)

Legend:
█ = Stone (mountain)
▓ = Mixed terrain (steep slope)
░ = Beach transition
· = Ocean floor
▒ = Water

Extended blend distance:
baseWidth = 100 blocks
heightDelta = 150 - 50 = 100 blocks
blendWidth = 100 * (1 + 100/50) = 300 blocks!
```

**Special handling:** Extreme height differences get wider blend zones

---

## Diagram 9: Three-Biome Intersection (Voronoi-style)

### Top-Down View of Biome Cells

```
    Desert          Forest          Plains
    (90,5)         (55,70)         (50,45)
      │               │               │
      ▼               ▼               ▼
  ╔═══════╗       ╔═══════╗       ╔═══════╗
  ║▓▓▓▓▓▓▓║       ║░░░░░░░║       ║▒▒▒▒▒▒▒║
  ║▓▓▓▓▓▓▓║       ║░░░░░░░║       ║▒▒▒▒▒▒▒║
  ║▓▓▓▓▓▓▓╠───────╬░░░░░░░╠───────╬▒▒▒▒▒▒▒║
  ║▓▓▓▓▓▓▓│ ░▒▓  │░░░░░░░│  ▒░▓ │▒▒▒▒▒▒▒║
  ║▓▓▓▓▓▓▓│  ▒░▓ │░░░░░░░│  ░▒▓ │▒▒▒▒▒▒▒║
  ╠═══════╡   ▒▓░│   X   │  ▒░  ╞═══════╣
  │ ▓▒░   │   ░▓▒│ ░▒▓   │ ░▒▓  │   ░▒▓ │
  │  ▓▒░  │    ▓▒░░░░▒▓  │░▒▓   │  ░▒▓  │
  │   ▓▒░ │     ▓▒░░░▓   │▒▓    │ ░▒▓   │
  └────────       ▓▒░     ─────────░▒▓───┘
       │            ▓▒    │     │
       └─────────────▓────┴─────┘
              Center point X

At point X:
Desert:  0.33 (33%)
Forest:  0.40 (40%)
Plains:  0.27 (27%)

Result: Forest-influenced terrain with desert/plains mixing
```

---

## Diagram 10: Performance Profile

### Chunk Generation Time Breakdown

```
WITHOUT BLENDING (Current):
┌─────────────────────────────────────┐
│ Terrain Height Lookup   ███ 3ms     │
│ Cave Generation        ████ 4ms     │
│ Block Placement       █████ 5ms     │
│ Mesh Generation    ████████ 8ms     │
│ Buffer Upload         ████ 4ms      │
└─────────────────────────────────────┘
Total: ~24ms per chunk

WITH BLENDING (Proposed):
┌─────────────────────────────────────┐
│ Biome Influence Calc   ████ 4ms     │
│ Blended Height Lookup  ████ 4ms     │
│ Cave Generation        ████ 4ms     │
│ Block Placement       ██████ 6ms    │
│ Mesh Generation    ████████ 8ms     │
│ Buffer Upload         ████ 4ms      │
└─────────────────────────────────────┘
Total: ~30ms per chunk (+25%)

WITH CACHING (Optimized):
┌─────────────────────────────────────┐
│ Biome Influence (cached) ██ 2ms     │
│ Blended Height (cached)  ██ 2ms     │
│ Cave Generation          ████ 4ms   │
│ Block Placement         █████ 5ms   │
│ Mesh Generation      ████████ 8ms   │
│ Buffer Upload           ████ 4ms    │
└─────────────────────────────────────┘
Total: ~25ms per chunk (+4% only!)
```

**Key insight:** Aggressive caching reduces overhead to near zero

---

## Diagram 11: Memory Usage Map

```
Component           Size per Entry    Max Entries    Total Memory
─────────────────────────────────────────────────────────────────
BiomeInfluence      40 bytes         100,000         ~4 MB
TerrainHeight       4 bytes          100,000         ~0.4 MB
BlockCache          4 bytes          50,000          ~0.2 MB
─────────────────────────────────────────────────────────────────
Total Blending:                                      ~4.6 MB

For comparison:
- Current biome cache:    ~3 MB
- Chunk mesh data:        ~500 MB (for 1000 chunks)
- Total game memory:      ~2 GB

Blending overhead: 0.2% of total memory (negligible)
```

---

## Diagram 12: Biome Identity Preservation

### Ensuring Core Biome Areas Remain Pure

```
              Biome Boundary
                    │
    Pure Desert     │     Transition      │    Pure Forest
    (100% sand)     │    (50-50 mix)      │   (100% grass)
                    │                     │
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓░░░░░░░░·····│·················
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓░░░░░░░░······│·················
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓▓▓░░░░░░░·······│·················
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓▓░░░░░░········│·················
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓░░░░░·········│·················
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓▓░░░░··········│·················
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│▓░░···········│·················
                    │                   │
    ←────────────────→←─────────────────→←───────────────→
       600 blocks     100 blocks (blend)    600 blocks

Influence threshold for pure biome:
- Desert > 95% → Pure desert features
- Forest > 95% → Pure forest features
- Neither > 95% → Blended transition
```

**Design principle:** Most of each biome stays pure, only edges blend

---

## Summary: Visual Design Principles

### ✅ Good Blending
```
Desert → Transition → Forest
████████░░░░▓▓▓▓········
└──────┴────┴────┴──────
  Pure   Mix   Mix  Pure

- Gradual change over 100+ blocks
- Smooth height transitions
- Natural appearance
```

### ❌ Bad Blending
```
Desert │ Forest
████████│········
        │
   Hard line
   (current system)

- Instant change
- Height cliffs
- Unrealistic
```

---

**End of Diagrams Document**

These diagrams should be used alongside:
- BIOME_BLENDING_DESIGN.md (full technical spec)
- BIOME_BLENDING_QUICK_REFERENCE.md (implementation guide)
