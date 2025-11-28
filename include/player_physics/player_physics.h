/**
 * @file player_physics.h
 * @brief Convenience header including all player physics systems
 *
 * Include this single header to get access to:
 * - PlayerModelPhysics: Unified controller
 * - BoneCollisionManager: Per-bone collision capsules
 * - SquishSystem: Squash/stretch deformation
 * - HeadTracking: Procedural head look
 * - CenterOfGravity: Physics at spine_root
 */

#pragma once

#include "player_physics/player_model_physics.h"
#include "player_physics/bone_collision.h"
#include "player_physics/squish_system.h"
#include "player_physics/head_tracking.h"
#include "player_physics/center_of_gravity.h"
