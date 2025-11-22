#include "debug_state.h"
#include <cmath>

DebugState::DebugState()
    : renderDebug("debug_render", "Show chunk rendering debug info", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      drawFPS("debug_drawfps", "Show FPS counter", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      showTargetInfo("debug_targetinfo", "Show target information", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      showCullingStats("debug_culling", "Show frustum culling statistics", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      debugCollision("debug_collision", "Show collision detection debug output", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      debugWorld("debug_world", "Show world/chunk debug logging", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      wireframeMode("wireframe", "Enable wireframe rendering mode", false, FCVAR_NOTIFY),
      lightingEnabled("lighting", "Enable/disable voxel lighting system", true, FCVAR_ARCHIVE | FCVAR_NOTIFY) {
}

DebugState& DebugState::instance() {
    static DebugState instance;
    return instance;
}

void DebugState::updateFPS(float deltaTime) {
    // BUG FIX: Guard against invalid deltaTime values (NaN, negative, or zero)
    // Prevents division by zero and FPS calculation issues
    if (deltaTime <= 0.0f || std::isnan(deltaTime) || std::isinf(deltaTime)) {
        return;  // Skip this frame's FPS calculation
    }

    frameCount++;
    fpsUpdateTimer += deltaTime;

    // Update FPS counter every 0.5 seconds
    if (fpsUpdateTimer >= 0.5f) {
        lastFPS = frameCount / fpsUpdateTimer;
        frameCount = 0;
        fpsUpdateTimer = 0.0f;
    }
}
