#include "debug_state.h"

DebugState::DebugState()
    : renderDebug("debug_render", "Show chunk rendering debug info", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      drawFPS("debug_drawfps", "Show FPS counter", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      showTargetInfo("debug_targetinfo", "Show target information", false, FCVAR_ARCHIVE | FCVAR_NOTIFY),
      noclip("noclip", "Enable noclip mode", false, FCVAR_NOTIFY) {
}

DebugState& DebugState::instance() {
    static DebugState instance;
    return instance;
}

void DebugState::updateFPS(float deltaTime) {
    frameCount++;
    fpsUpdateTimer += deltaTime;

    // Update FPS counter every 0.5 seconds
    if (fpsUpdateTimer >= 0.5f) {
        lastFPS = frameCount / fpsUpdateTimer;
        frameCount = 0;
        fpsUpdateTimer = 0.0f;
    }
}
