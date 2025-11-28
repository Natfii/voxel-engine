/**
 * @file editor_background.cpp
 * @brief Implementation of cute animated procedural 2D background
 *
 * Features:
 * - FastNoiseLite terrain generation
 * - Mountains with snow caps
 * - Beaches and islands
 * - Deserts
 * - Boats on water
 * - Towns with people
 */

#include "editor/editor_background.h"
#include "FastNoiseLite.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <random>

EditorBackground::EditorBackground() {
    std::random_device rd;
    m_seed = rd();
}

void EditorBackground::initialize(int width, int height) {
    m_width = width;
    m_height = height;
    m_pixels.resize(width * height);
    m_elevation.resize(width * height);
    m_moisture.resize(width * height);

    generateTerrain();
    spawnTowns();
    spawnTrees();
    spawnBoats();
    spawnPeople(50);  // 50 little wandering people

    m_needsUpload = true;
}

float EditorBackground::noise2D(float x, float y, int octaves) {
    // This is now just a fallback - we use FastNoiseLite in generateTerrain
    return 0.0f;
}

uint32_t EditorBackground::getBiomeColor(float elevation, float moisture) {
    // Color palette (ABGR format for ImGui)
    const uint32_t DEEP_WATER    = IM_COL32(20, 50, 120, 255);   // Deep blue
    const uint32_t SHALLOW_WATER = IM_COL32(50, 100, 180, 255);  // Light blue
    const uint32_t BEACH         = IM_COL32(230, 210, 160, 255); // Sandy tan
    const uint32_t DESERT        = IM_COL32(210, 180, 100, 255); // Desert yellow
    const uint32_t GRASS_LIGHT   = IM_COL32(120, 180, 80, 255);  // Light green
    const uint32_t GRASS_DARK    = IM_COL32(60, 140, 50, 255);   // Forest green
    const uint32_t FOREST        = IM_COL32(30, 100, 30, 255);   // Dark green
    const uint32_t ROCK          = IM_COL32(128, 128, 128, 255); // Gray
    const uint32_t ROCK_DARK     = IM_COL32(90, 90, 90, 255);    // Dark gray
    const uint32_t SNOW          = IM_COL32(250, 250, 255, 255); // White/bluish

    // Water
    if (elevation < 0.32f) {
        return DEEP_WATER;
    } else if (elevation < 0.38f) {
        return SHALLOW_WATER;
    }
    // Beach (narrow strip)
    else if (elevation < 0.42f) {
        return BEACH;
    }
    // Low lands
    else if (elevation < 0.55f) {
        if (moisture < 0.25f) return DESERT;      // Dry = desert
        if (moisture > 0.65f) return FOREST;       // Wet = forest
        if (moisture > 0.4f) return GRASS_DARK;    // Medium = grass
        return GRASS_LIGHT;                        // Light grass
    }
    // Hills
    else if (elevation < 0.72f) {
        if (moisture < 0.2f) return DESERT;
        if (moisture > 0.5f) return FOREST;
        return GRASS_DARK;
    }
    // Mountains
    else if (elevation < 0.85f) {
        return ROCK;
    }
    // High mountains
    else if (elevation < 0.92f) {
        return ROCK_DARK;
    }
    // Snow caps
    else {
        return SNOW;
    }
}

void EditorBackground::generateTerrain() {
    // Use FastNoiseLite for better terrain
    FastNoiseLite elevationNoise(m_seed);
    elevationNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    elevationNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    elevationNoise.SetFractalOctaves(5);
    elevationNoise.SetFrequency(0.008f);

    FastNoiseLite moistureNoise(m_seed + 1000);
    moistureNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    moistureNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    moistureNoise.SetFractalOctaves(4);
    moistureNoise.SetFrequency(0.012f);

    // Mountain ridge noise for dramatic peaks
    FastNoiseLite ridgeNoise(m_seed + 2000);
    ridgeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    ridgeNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    ridgeNoise.SetFractalOctaves(3);
    ridgeNoise.SetFrequency(0.015f);

    for (int y = 0; y < m_height; y++) {
        for (int x = 0; x < m_width; x++) {
            int idx = y * m_width + x;

            // Base elevation
            float e = elevationNoise.GetNoise((float)x, (float)y);
            e = (e + 1.0f) * 0.5f;  // Normalize to 0-1

            // Add mountain ridges
            float ridge = ridgeNoise.GetNoise((float)x, (float)y);
            ridge = (ridge + 1.0f) * 0.5f;

            // Blend ridges into high areas
            if (e > 0.5f) {
                e = e + ridge * 0.3f * (e - 0.5f) * 2.0f;
            }

            // Create islands by fading edges
            float cx = (float)x / m_width - 0.5f;
            float cy = (float)y / m_height - 0.5f;
            float edgeDist = 1.0f - std::sqrt(cx*cx + cy*cy) * 1.5f;
            edgeDist = std::clamp(edgeDist, 0.0f, 1.0f);
            e *= edgeDist;

            // Moisture
            float m = moistureNoise.GetNoise((float)x, (float)y);
            m = (m + 1.0f) * 0.5f;

            m_elevation[idx] = std::clamp(e, 0.0f, 1.0f);
            m_moisture[idx] = m;
            m_pixels[idx] = getBiomeColor(m_elevation[idx], m);
        }
    }
}

void EditorBackground::spawnTowns() {
    m_towns.clear();
    std::mt19937 rng(m_seed + 500);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Try to place several towns on suitable land
    int townsPlaced = 0;
    int maxTowns = 5;
    int attempts = 0;

    while (townsPlaced < maxTowns && attempts < 200) {
        attempts++;
        int x = 30 + (int)(dist(rng) * (m_width - 60));
        int y = 30 + (int)(dist(rng) * (m_height - 60));
        int idx = y * m_width + x;

        // Towns on low-medium elevation land (not beaches, not mountains)
        if (m_elevation[idx] > 0.45f && m_elevation[idx] < 0.65f) {
            // Check it's not too close to other towns
            bool tooClose = false;
            for (const auto& town : m_towns) {
                float dx = town.x - x;
                float dy = town.y - y;
                if (std::sqrt(dx*dx + dy*dy) < 40) {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose) {
                Town town;
                town.x = (float)x;
                town.y = (float)y;
                town.size = 3 + (int)(dist(rng) * 4);  // 3-6 buildings
                m_towns.push_back(town);
                townsPlaced++;
            }
        }
    }
}

void EditorBackground::spawnTrees() {
    m_trees.clear();
    std::mt19937 rng(m_seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int y = 0; y < m_height; y += 6) {
        for (int x = 0; x < m_width; x += 6) {
            int idx = y * m_width + x;
            float e = m_elevation[idx];
            float m = m_moisture[idx];

            // Trees on grass/forest biomes, not too close to towns
            if (e > 0.42f && e < 0.75f && m > 0.3f) {
                // Check distance from towns
                bool nearTown = false;
                for (const auto& town : m_towns) {
                    float dx = town.x - x;
                    float dy = town.y - y;
                    if (std::sqrt(dx*dx + dy*dy) < 15) {
                        nearTown = true;
                        break;
                    }
                }

                if (!nearTown && dist(rng) < 0.35f) {
                    Tree tree;
                    tree.position = glm::vec2(x + dist(rng) * 4, y + dist(rng) * 4);
                    tree.swayOffset = dist(rng) * 6.28f;
                    tree.type = (m > 0.6f) ? 0 : 1;  // Pine in wet areas, oak otherwise
                    m_trees.push_back(tree);
                }
            }
        }
    }
}

void EditorBackground::spawnBoats() {
    m_boats.clear();
    std::mt19937 rng(m_seed + 300);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Spawn boats on water
    int boatsPlaced = 0;
    int maxBoats = 8;
    int attempts = 0;

    while (boatsPlaced < maxBoats && attempts < 100) {
        attempts++;
        int x = 20 + (int)(dist(rng) * (m_width - 40));
        int y = 20 + (int)(dist(rng) * (m_height - 40));
        int idx = y * m_width + x;

        // Boats on shallow water near coasts
        if (m_elevation[idx] > 0.25f && m_elevation[idx] < 0.38f) {
            Boat boat;
            boat.position = glm::vec2((float)x, (float)y);
            boat.angle = dist(rng) * 6.28f;
            boat.bobOffset = dist(rng) * 6.28f;
            m_boats.push_back(boat);
            boatsPlaced++;
        }
    }
}

void EditorBackground::spawnPeople(int count) {
    m_people.clear();
    std::mt19937 rng(m_seed + 1);
    std::uniform_real_distribution<float> distX(20.0f, (float)m_width - 20);
    std::uniform_real_distribution<float> distY(20.0f, (float)m_height - 20);
    std::uniform_real_distribution<float> speed(8.0f, 20.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        LittlePerson person;
        int attempts = 0;
        const int maxAttempts = 100;
        bool foundSpot = false;

        // 60% of people spawn near towns
        bool spawnNearTown = !m_towns.empty() && dist01(rng) < 0.6f;

        while (attempts < maxAttempts) {
            if (spawnNearTown) {
                // Pick a random town
                const auto& town = m_towns[(int)(dist01(rng) * m_towns.size()) % m_towns.size()];
                person.position = glm::vec2(
                    town.x + (dist01(rng) - 0.5f) * 30,
                    town.y + (dist01(rng) - 0.5f) * 30
                );
            } else {
                person.position = glm::vec2(distX(rng), distY(rng));
            }

            int idx = (int)person.position.y * m_width + (int)person.position.x;
            if (idx >= 0 && idx < (int)m_elevation.size() &&
                m_elevation[idx] > 0.4f && m_elevation[idx] < 0.85f) {
                foundSpot = true;
                break;
            }
            attempts++;
        }

        if (!foundSpot) continue;

        person.target = person.position;
        person.speed = speed(rng);
        person.waiting = true;
        person.waitTime = 0.5f;
        m_people.push_back(person);
    }
}

void EditorBackground::updatePeople(float deltaTime) {
    std::mt19937 rng(static_cast<unsigned int>(m_time * 1000));
    std::uniform_real_distribution<float> dist(-40.0f, 40.0f);
    std::uniform_real_distribution<float> waitDist(0.5f, 4.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    for (auto& person : m_people) {
        if (person.waiting) {
            person.waitTime -= deltaTime;
            if (person.waitTime <= 0) {
                person.waiting = false;

                // 40% chance to go to a town
                bool goToTown = !m_towns.empty() && dist01(rng) < 0.4f;

                glm::vec2 newTarget;
                int attempts = 0;
                do {
                    if (goToTown) {
                        const auto& town = m_towns[(int)(dist01(rng) * m_towns.size()) % m_towns.size()];
                        newTarget = glm::vec2(
                            town.x + (dist01(rng) - 0.5f) * 20,
                            town.y + (dist01(rng) - 0.5f) * 20
                        );
                    } else {
                        newTarget = person.position + glm::vec2(dist(rng), dist(rng));
                    }
                    newTarget.x = std::clamp(newTarget.x, 10.0f, (float)m_width - 10);
                    newTarget.y = std::clamp(newTarget.y, 10.0f, (float)m_height - 10);

                    int idx = (int)newTarget.y * m_width + (int)newTarget.x;
                    if (idx >= 0 && idx < (int)m_elevation.size() &&
                        m_elevation[idx] > 0.4f && m_elevation[idx] < 0.85f) {
                        person.target = newTarget;
                        break;
                    }
                    attempts++;
                } while (attempts < 15);
            }
        } else {
            glm::vec2 dir = person.target - person.position;
            float d = glm::length(dir);
            if (d < 2.0f) {
                person.waiting = true;
                person.waitTime = waitDist(rng);
            } else {
                dir = glm::normalize(dir);
                person.position += dir * person.speed * deltaTime;
            }
        }
    }
}

void EditorBackground::update(float deltaTime) {
    if (m_paused) return;
    m_time += deltaTime;
    m_windTime += deltaTime * 2.0f;
    updatePeople(deltaTime);
    m_needsUpload = true;
}

void EditorBackground::render() {
    if (m_width == 0 || m_height == 0) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Calculate scale to fill the available window space
    float scaleX = displaySize.x / (float)m_width;
    float scaleY = displaySize.y / (float)m_height;

    // Use uniform scaling to maintain aspect ratio, or stretch to fill:
    // Uncomment the following line to maintain aspect ratio:
    // float pixelSize = std::min(scaleX, scaleY);
    // Or use this to stretch and fill:
    float pixelSizeX = scaleX;
    float pixelSizeY = scaleY;

    // Dark background
    drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(15, 20, 30, 255));

    // Draw terrain
    const int step = 2;
    for (int y = 0; y < m_height; y += step) {
        for (int x = 0; x < m_width; x += step) {
            int idx = y * m_width + x;
            uint32_t col = m_pixels[idx];

            float px = x * pixelSizeX;
            float py = y * pixelSizeY;
            float sizeX = pixelSizeX * step;
            float sizeY = pixelSizeY * step;

            drawList->AddRectFilled(ImVec2(px, py), ImVec2(px + sizeX, py + sizeY), col);
        }
    }

    // Draw towns (small brown rectangles for buildings)
    for (const auto& town : m_towns) {
        float tx = town.x * pixelSizeX;
        float ty = town.y * pixelSizeY;

        // Draw a cluster of tiny buildings
        std::mt19937 townRng((int)(town.x * 1000 + town.y));
        std::uniform_real_distribution<float> d(-8.0f, 8.0f);

        for (int b = 0; b < town.size; b++) {
            float bx = tx + d(townRng) * pixelSizeX;
            float by = ty + d(townRng) * pixelSizeY;
            float bw = (2 + (townRng() % 3)) * pixelSizeX;
            float bh = (2 + (townRng() % 3)) * pixelSizeY;

            // Building color (brownish)
            ImU32 buildingCol = IM_COL32(120 + (townRng() % 40), 80 + (townRng() % 30), 50 + (townRng() % 20), 255);
            drawList->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), buildingCol);
        }
    }

    // Draw trees
    for (const auto& tree : m_trees) {
        float sway = sinf(m_windTime + tree.swayOffset) * 0.4f;
        float tx = (tree.position.x + sway) * pixelSizeX;
        float ty = tree.position.y * pixelSizeY;

        ImU32 treeColor = (tree.type == 0) ? IM_COL32(20, 70, 20, 255) : IM_COL32(40, 100, 40, 255);
        drawList->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + pixelSizeX * 2, ty + pixelSizeY * 2), treeColor);
    }

    // Draw boats (brown rectangles on water, bobbing)
    for (const auto& boat : m_boats) {
        float bob = sinf(m_time * 2.0f + boat.bobOffset) * 1.0f;
        float bx = boat.position.x * pixelSizeX;
        float by = (boat.position.y + bob) * pixelSizeY;

        // Simple boat shape - brown rectangle
        drawList->AddRectFilled(
            ImVec2(bx, by),
            ImVec2(bx + pixelSizeX * 4, by + pixelSizeY * 2),
            IM_COL32(100, 60, 30, 255)
        );
    }

    // Draw people (black pixels)
    for (const auto& person : m_people) {
        float px = person.position.x * pixelSizeX;
        float py = person.position.y * pixelSizeY;

        drawList->AddRectFilled(
            ImVec2(px, py),
            ImVec2(px + pixelSizeX * 2, py + pixelSizeY * 2),
            IM_COL32(0, 0, 0, 255)
        );
    }

    // UI: Pause checkbox in bottom-right
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - 160, displaySize.y - 35), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##BgControls", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::Checkbox("Pause Background", &m_paused);
    ImGui::End();
}

void EditorBackground::regenerate() {
    std::random_device rd;
    m_seed = rd();
    generateTerrain();
    spawnTowns();
    spawnTrees();
    spawnBoats();
    spawnPeople(50);
    m_needsUpload = true;
}
