/**
 * @file event_types.cpp
 * @brief Implementation of event type constructors
 */

#include "event_types.h"
#include <chrono>

// Helper function to get current timestamp
static double getCurrentTimestamp() {
    using namespace std::chrono;
    auto now = steady_clock::now();
    auto dur = now.time_since_epoch();
    return duration_cast<duration<double, std::ratio<1>>>(dur).count();
}

// ============================================================================
// Base Event
// ============================================================================

Event::Event(EventType t)
    : type(t)
    , cancelled(false)
    , timestamp(getCurrentTimestamp())
{}

// ============================================================================
// Block Events
// ============================================================================

BlockBreakEvent::BlockBreakEvent(glm::ivec3 pos, int block, BreakCause c, int breaker)
    : Event(EventType::BLOCK_BREAK)
    , position(pos)
    , blockID(block)
    , cause(c)
    , breakerEntityID(breaker)
{}

BlockPlaceEvent::BlockPlaceEvent(glm::ivec3 pos, int block, int placer, glm::ivec3 against)
    : Event(EventType::BLOCK_PLACE)
    , position(pos)
    , blockID(block)
    , placerEntityID(placer)
    , placedAgainst(against)
{}

BlockInteractEvent::BlockInteractEvent(glm::ivec3 pos, int block, int entity, bool rightClick, int heldItem)
    : Event(EventType::BLOCK_INTERACT)
    , position(pos)
    , blockID(block)
    , entityID(entity)
    , isRightClick(rightClick)
    , heldItemID(heldItem)
{}

BlockStepEvent::BlockStepEvent(glm::ivec3 pos, int block, int entity)
    : Event(EventType::BLOCK_STEP)
    , position(pos)
    , blockID(block)
    , entityID(entity)
{}

BlockUpdateEvent::BlockUpdateEvent(glm::ivec3 pos, int block)
    : Event(EventType::BLOCK_UPDATE)
    , position(pos)
    , blockID(block)
{}

// ============================================================================
// Neighbor Events
// ============================================================================

NeighborChangedEvent::NeighborChangedEvent(glm::ivec3 pos, glm::ivec3 neighbor, int oldBlock, int newBlock)
    : Event(EventType::NEIGHBOR_CHANGED)
    , position(pos)
    , neighborPos(neighbor)
    , oldBlockID(oldBlock)
    , newBlockID(newBlock)
{}

// ============================================================================
// Chunk Events
// ============================================================================

ChunkLoadEvent::ChunkLoadEvent(int x, int y, int z, bool isNew)
    : Event(EventType::CHUNK_LOAD)
    , chunkX(x)
    , chunkY(y)
    , chunkZ(z)
    , isNewChunk(isNew)
{}

ChunkUnloadEvent::ChunkUnloadEvent(int x, int y, int z)
    : Event(EventType::CHUNK_UNLOAD)
    , chunkX(x)
    , chunkY(y)
    , chunkZ(z)
{}

// ============================================================================
// Player Events
// ============================================================================

PlayerMoveEvent::PlayerMoveEvent(glm::vec3 oldPos, glm::vec3 newPos, int player)
    : Event(EventType::PLAYER_MOVE)
    , oldPosition(oldPos)
    , newPosition(newPos)
    , playerID(player)
{}

PlayerJumpEvent::PlayerJumpEvent(glm::vec3 pos, int player)
    : Event(EventType::PLAYER_JUMP)
    , position(pos)
    , playerID(player)
{}

PlayerLandEvent::PlayerLandEvent(glm::vec3 pos, float fall, int player)
    : Event(EventType::PLAYER_LAND)
    , position(pos)
    , fallDistance(fall)
    , playerID(player)
{}

// ============================================================================
// Time Events
// ============================================================================

TimeChangeEvent::TimeChangeEvent(float oldT, float newT)
    : Event(EventType::TIME_CHANGE)
    , oldTime(oldT)
    , newTime(newT)
{}

// ============================================================================
// Custom Events
// ============================================================================

CustomEvent::CustomEvent(const std::string& name, std::any eventData)
    : Event(EventType::CUSTOM)
    , eventName(name)
    , data(std::move(eventData))
{}
