/**
 * @file event_types.h
 * @brief Event system core types for the voxel engine
 *
 * This file defines all event types used in the voxel engine's event system.
 * The design is inspired by Minecraft Forge's event system, providing a flexible
 * and extensible way to handle game events.
 *
 * Events can be:
 * - Cancelled (for cancellable events) to prevent default behavior
 * - Filtered based on custom predicates
 * - Handled by multiple listeners in priority order
 *
 * @note All event positions use world coordinates (not chunk-relative)
 */

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <functional>
#include <any>

/**
 * @brief Enumeration of all event types in the system
 *
 * Each event type represents a specific occurrence in the game world.
 * Events are organized into categories: Block, Neighbor, World, Player, Time, and Custom.
 */
enum class EventType {
    // Block Events
    BLOCK_BREAK,          ///< Fires when a block is broken (cancellable)
    BLOCK_PLACE,          ///< Fires when a block is placed (cancellable)
    BLOCK_INTERACT,       ///< Fires when a player right-clicks a block (cancellable)
    BLOCK_STEP,           ///< Fires when an entity steps on top of a block
    BLOCK_UPDATE,         ///< Fires when a block receives a scheduled update tick

    // Neighbor Events (fired when adjacent blocks change)
    NEIGHBOR_CHANGED,     ///< Fires when an adjacent block changes state
    NEIGHBOR_PLACED,      ///< Fires when a new block is placed adjacent to this one
    NEIGHBOR_BROKEN,      ///< Fires when an adjacent block is broken

    // World Events
    CHUNK_LOAD,           ///< Fires when a chunk is loaded (generation or from disk)
    CHUNK_UNLOAD,         ///< Fires when a chunk is unloaded
    WORLD_SAVE,           ///< Fires when the world is being saved
    WORLD_LOAD,           ///< Fires when the world is being loaded

    // Player Events
    PLAYER_MOVE,          ///< Fires when a player moves (cancellable)
    PLAYER_JUMP,          ///< Fires when a player jumps
    PLAYER_LAND,          ///< Fires when a player lands after falling
    PLAYER_SWIM,          ///< Fires when a player is swimming in water

    // Time Events
    TIME_CHANGE,          ///< Fires when the world time changes
    DAY_START,            ///< Fires at sunrise
    NIGHT_START,          ///< Fires at sunset

    // Custom/Script Events
    CUSTOM                ///< Custom event for scripting and mods
};

/**
 * @brief Enumeration of block break causes
 *
 * Indicates what caused a block to break. Used in BlockBreakEvent to provide
 * context for the break action.
 */
enum class BreakCause {
    PLAYER,      ///< Block broken by player action
    EXPLOSION,   ///< Block destroyed by explosion
    WATER,       ///< Block washed away by water
    GRAVITY,     ///< Block fell due to gravity (e.g., sand, gravel)
    SCRIPT,      ///< Block broken by script/mod
    UNKNOWN      ///< Unknown or unspecified cause
};

/**
 * @brief Base class for all events
 *
 * Provides common functionality for all event types including:
 * - Event type identification
 * - Cancellation support
 * - Timestamp tracking
 *
 * All specific event types inherit from this base class.
 */
struct Event {
    EventType type;           ///< Type of this event
    bool cancelled = false;   ///< Whether this event has been cancelled
    double timestamp;         ///< Time when event was created (game time)

    /**
     * @brief Construct a new Event
     * @param t The event type
     */
    Event(EventType t);

    virtual ~Event() = default;

    /**
     * @brief Cancel this event
     *
     * Cancelling an event prevents its default behavior from executing.
     * Not all events are cancellable.
     */
    void cancel() { cancelled = true; }

    /**
     * @brief Check if this event has been cancelled
     * @return true if cancelled, false otherwise
     */
    bool isCancelled() const { return cancelled; }
};

// ============================================================================
// Block Events
// ============================================================================

/**
 * @brief Event fired when a block is broken
 *
 * This event is cancellable. Cancelling it will prevent the block from breaking.
 * Fires before the block is removed from the world.
 *
 * Use cases:
 * - Prevent breaking of protected blocks
 * - Drop custom items on block break
 * - Trigger effects when specific blocks are broken
 */
struct BlockBreakEvent : Event {
    glm::ivec3 position;      ///< World position of the block being broken
    int blockID;              ///< ID of the block being broken
    BreakCause cause;         ///< What caused the block to break
    int breakerEntityID;      ///< Entity ID of the breaker, -1 if not an entity

    /**
     * @brief Construct a BlockBreakEvent
     * @param pos World position of the block
     * @param block Block ID
     * @param c Cause of the break
     * @param breaker Entity ID of breaker, or -1 if none
     */
    BlockBreakEvent(glm::ivec3 pos, int block, BreakCause c, int breaker = -1);
};

/**
 * @brief Event fired when a block is placed
 *
 * This event is cancellable. Cancelling it will prevent the block from being placed.
 * Fires before the block is added to the world.
 *
 * Use cases:
 * - Prevent placement in protected areas
 * - Validate block placement rules
 * - Trigger effects when blocks are placed
 */
struct BlockPlaceEvent : Event {
    glm::ivec3 position;      ///< World position where block will be placed
    int blockID;              ///< ID of the block being placed
    int placerEntityID;       ///< Entity ID of the placer
    glm::ivec3 placedAgainst; ///< Position of block this was placed against

    /**
     * @brief Construct a BlockPlaceEvent
     * @param pos World position for placement
     * @param block Block ID to place
     * @param placer Entity ID of placer
     * @param against Position of block being placed against
     */
    BlockPlaceEvent(glm::ivec3 pos, int block, int placer, glm::ivec3 against);
};

/**
 * @brief Event fired when a player interacts with a block
 *
 * This event is cancellable and fires when a player right-clicks a block with
 * either an empty hand or a non-placeable item.
 *
 * Use cases:
 * - Open custom GUIs (chests, furnaces, etc.)
 * - Trigger block-specific actions (buttons, levers, doors)
 * - Handle tool interactions
 */
struct BlockInteractEvent : Event {
    glm::ivec3 position;      ///< World position of the interacted block
    int blockID;              ///< ID of the block being interacted with
    int entityID;             ///< Entity ID performing the interaction
    bool isRightClick;        ///< True for right-click, false for left-click
    int heldItemID;           ///< ID of item held, -1 if empty hand

    /**
     * @brief Construct a BlockInteractEvent
     * @param pos World position of block
     * @param block Block ID
     * @param entity Entity ID performing interaction
     * @param rightClick True if right-click interaction
     * @param heldItem Item ID held, or -1 for empty hand
     */
    BlockInteractEvent(glm::ivec3 pos, int block, int entity, bool rightClick, int heldItem = -1);
};

/**
 * @brief Event fired when an entity steps on a block
 *
 * Fires continuously while an entity is standing on a block.
 * Useful for pressure plates, farmland trampling, etc.
 *
 * Use cases:
 * - Pressure plates
 * - Farmland trampling
 * - Speed/jump boost blocks
 * - Damage floors (lava, magma blocks)
 */
struct BlockStepEvent : Event {
    glm::ivec3 position;      ///< World position of the block being stepped on
    int blockID;              ///< ID of the block
    int entityID;             ///< Entity ID stepping on the block

    /**
     * @brief Construct a BlockStepEvent
     * @param pos World position of block
     * @param block Block ID
     * @param entity Entity ID stepping on block
     */
    BlockStepEvent(glm::ivec3 pos, int block, int entity);
};

/**
 * @brief Event fired when a block receives an update tick
 *
 * Block updates are scheduled ticks that allow blocks to perform periodic actions.
 * Examples: crop growth, liquid flow, redstone updates.
 *
 * Use cases:
 * - Crop growth ticks
 * - Liquid flow simulation
 * - Redstone signal propagation
 * - Random block updates
 */
struct BlockUpdateEvent : Event {
    glm::ivec3 position;      ///< World position of the block receiving update
    int blockID;              ///< ID of the block

    /**
     * @brief Construct a BlockUpdateEvent
     * @param pos World position of block
     * @param block Block ID
     */
    BlockUpdateEvent(glm::ivec3 pos, int block);
};

// ============================================================================
// Neighbor Events
// ============================================================================

/**
 * @brief Event fired when a neighboring block changes
 *
 * Sent to blocks when an adjacent block (6-directional neighbors) changes state.
 * This allows blocks to react to their environment.
 *
 * Use cases:
 * - Redstone wire updating
 * - Torches popping off when support breaks
 * - Water/lava flow triggers
 * - Grass spreading or dying
 */
struct NeighborChangedEvent : Event {
    glm::ivec3 position;       ///< World position of block receiving notification
    glm::ivec3 neighborPos;    ///< World position of neighbor that changed
    int oldBlockID;            ///< Previous block ID at neighbor position
    int newBlockID;            ///< New block ID at neighbor position

    /**
     * @brief Construct a NeighborChangedEvent
     * @param pos Position of block receiving notification
     * @param neighbor Position of changed neighbor
     * @param oldBlock Previous block ID at neighbor position
     * @param newBlock New block ID at neighbor position
     */
    NeighborChangedEvent(glm::ivec3 pos, glm::ivec3 neighbor, int oldBlock, int newBlock);
};

// ============================================================================
// Chunk Events
// ============================================================================

/**
 * @brief Event fired when a chunk is loaded
 *
 * Fires after a chunk is fully loaded and ready for use.
 * The isNewChunk flag indicates whether this is a newly generated chunk
 * or one loaded from disk.
 *
 * Use cases:
 * - Initialize chunk-specific data structures
 * - Populate newly generated chunks with entities
 * - Schedule block updates for loaded chunks
 */
struct ChunkLoadEvent : Event {
    int chunkX, chunkY, chunkZ;  ///< Chunk coordinates
    bool isNewChunk;             ///< True if newly generated, false if loaded from disk

    /**
     * @brief Construct a ChunkLoadEvent
     * @param x Chunk X coordinate
     * @param y Chunk Y coordinate
     * @param z Chunk Z coordinate
     * @param isNew True if newly generated chunk
     */
    ChunkLoadEvent(int x, int y, int z, bool isNew);
};

/**
 * @brief Event fired when a chunk is unloaded
 *
 * Fires before a chunk is unloaded from memory.
 * This is the last chance to save chunk-specific data.
 *
 * Use cases:
 * - Save custom chunk data
 * - Clean up chunk-related resources
 * - Remove entities from unloading chunks
 */
struct ChunkUnloadEvent : Event {
    int chunkX, chunkY, chunkZ;  ///< Chunk coordinates

    /**
     * @brief Construct a ChunkUnloadEvent
     * @param x Chunk X coordinate
     * @param y Chunk Y coordinate
     * @param z Chunk Z coordinate
     */
    ChunkUnloadEvent(int x, int y, int z);
};

// ============================================================================
// Player Events
// ============================================================================

/**
 * @brief Event fired when a player moves
 *
 * This event is cancellable. Cancelling it will prevent the movement.
 * Fires for all player movement including walking, flying, and swimming.
 *
 * Use cases:
 * - Region protection
 * - Movement restrictions
 * - Teleportation triggers
 * - Anti-cheat validation
 */
struct PlayerMoveEvent : Event {
    glm::vec3 oldPosition;    ///< Player's previous position
    glm::vec3 newPosition;    ///< Player's new position (can be modified)
    int playerID;             ///< Player entity ID (0 for local player)

    /**
     * @brief Construct a PlayerMoveEvent
     * @param oldPos Previous position
     * @param newPos New position
     * @param player Player ID (default 0 for local player)
     */
    PlayerMoveEvent(glm::vec3 oldPos, glm::vec3 newPos, int player = 0);
};

/**
 * @brief Event fired when a player jumps
 *
 * Fires when a player initiates a jump.
 *
 * Use cases:
 * - Modify jump height
 * - Prevent jumping in certain areas
 * - Play custom jump sounds/effects
 * - Track player statistics
 */
struct PlayerJumpEvent : Event {
    glm::vec3 position;       ///< Position where jump occurred
    int playerID;             ///< Player entity ID (0 for local player)

    /**
     * @brief Construct a PlayerJumpEvent
     * @param pos Position of jump
     * @param player Player ID (default 0 for local player)
     */
    PlayerJumpEvent(glm::vec3 pos, int player = 0);
};

/**
 * @brief Event fired when a player lands after falling
 *
 * Fires when a player touches the ground after being airborne.
 * Includes fall distance for calculating fall damage.
 *
 * Use cases:
 * - Calculate fall damage
 * - Play landing sounds/particles
 * - Trigger ground slam abilities
 * - Break farmland on landing
 */
struct PlayerLandEvent : Event {
    glm::vec3 position;       ///< Position where player landed
    float fallDistance;       ///< Distance fallen in blocks
    int playerID;             ///< Player entity ID (0 for local player)

    /**
     * @brief Construct a PlayerLandEvent
     * @param pos Landing position
     * @param fall Fall distance in blocks
     * @param player Player ID (default 0 for local player)
     */
    PlayerLandEvent(glm::vec3 pos, float fall, int player = 0);
};

// ============================================================================
// Time Events
// ============================================================================

/**
 * @brief Event fired when world time changes
 *
 * Fires periodically as game time advances.
 * Time is normalized: 0.0 = midnight, 0.25 = dawn, 0.5 = noon, 0.75 = dusk.
 *
 * Use cases:
 * - Update time-dependent systems
 * - Trigger time-based events
 * - Sync client/server time
 */
struct TimeChangeEvent : Event {
    float oldTime;  ///< Previous time (0.0 = midnight, 0.5 = noon)
    float newTime;  ///< New time (0.0 = midnight, 0.5 = noon)

    /**
     * @brief Construct a TimeChangeEvent
     * @param oldT Previous time value
     * @param newT New time value
     */
    TimeChangeEvent(float oldT, float newT);
};

// ============================================================================
// Custom Events
// ============================================================================

/**
 * @brief Custom event for scripts and mods
 *
 * Allows scripts and mods to create their own event types dynamically.
 * The data field can hold any type using std::any for maximum flexibility.
 *
 * Use cases:
 * - Mod-specific events
 * - Scripted quest triggers
 * - Custom game mode events
 * - Inter-mod communication
 */
struct CustomEvent : Event {
    std::string eventName;    ///< Name identifier for this custom event
    std::any data;            ///< Custom data payload (any type)

    /**
     * @brief Construct a CustomEvent
     * @param name Event name identifier
     * @param eventData Custom data payload (default empty)
     */
    CustomEvent(const std::string& name, std::any eventData = {});
};

// ============================================================================
// Event System Types
// ============================================================================

/**
 * @brief Type alias for event callback functions
 *
 * Event callbacks receive a reference to the event, allowing them to
 * read event data and potentially cancel the event.
 */
using EventCallback = std::function<void(Event&)>;

/**
 * @brief Type alias for event filter functions
 *
 * Event filters return true if an event should be processed,
 * false if it should be skipped.
 */
using EventFilter = std::function<bool(const Event&)>;
