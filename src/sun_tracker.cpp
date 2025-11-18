#include "sun_tracker.h"
#include <cmath>

SunTracker::SunTracker()
    : m_sunAngle(0.0f),
      m_lastRecalculationAngle(0.0f),
      m_sunIntensity(0.0f),
      m_moonIntensity(1.0f),
      m_sunDirection(0.0f, -1.0f, 0.0f) {
}

void SunTracker::update(float deltaTime, float timeOfDay) {
    // Convert time (0-1) to angle (0-360 degrees)
    // timeOfDay: 0.0 = midnight, 0.25 = dawn, 0.5 = noon, 0.75 = dusk, 1.0 = midnight
    // sunAngle: 0 = noon (sun overhead), 180 = midnight (sun below horizon)
    //
    // We want:
    // - timeOfDay 0.0 (midnight) -> sunAngle 180°
    // - timeOfDay 0.5 (noon)     -> sunAngle 0°
    // - timeOfDay 1.0 (midnight) -> sunAngle 180° (wrap around)
    //
    // Formula: sunAngle = (0.5 - timeOfDay) * 360
    //          If negative, add 360 to keep in [0, 360) range
    m_sunAngle = (0.5f - timeOfDay) * 360.0f;
    if (m_sunAngle < 0.0f) {
        m_sunAngle += 360.0f;
    }

    // Calculate sun direction (sun rises in east, sets in west)
    // Sun path is in XY plane: X = east-west, Y = height, Z = north-south
    float angleRad = glm::radians(m_sunAngle);
    m_sunDirection = glm::normalize(glm::vec3(
        std::sin(angleRad),   // X: east-west movement
        std::cos(angleRad),   // Y: height in sky (1.0 at noon, -1.0 at midnight)
        0.0f                  // Z: sun path doesn't move north/south
    ));

    // Calculate sun/moon intensities based on time of day
    // Sun is active during day (0.25-0.75), moon during night (0.75-0.25 wrapped)
    //
    // Time ranges:
    // - 0.00-0.25: Night (moon active)
    // - 0.25-0.35: Dawn transition (sun rising)
    // - 0.35-0.65: Full day (sun at peak)
    // - 0.65-0.75: Dusk transition (sun setting)
    // - 0.75-1.00: Night (moon active)

    if (timeOfDay >= 0.25f && timeOfDay <= 0.75f) {
        // Daytime: Sun is active
        // Calculate day progress (0.0 at dawn, 0.5 at noon, 1.0 at dusk)
        float dayProgress = (timeOfDay - 0.25f) / 0.5f;  // Maps 0.25-0.75 -> 0.0-1.0

        // Sun intensity peaks at noon (dayProgress = 0.5)
        // Use smoothstep-like curve: intensity = 1.0 - 2*|progress - 0.5|
        // This gives: dawn=0.0, noon=1.0, dusk=0.0
        m_sunIntensity = 1.0f - std::abs(dayProgress - 0.5f) * 2.0f;
        m_sunIntensity = glm::clamp(m_sunIntensity, 0.0f, 1.0f);

        // No moon during day
        m_moonIntensity = 0.0f;
    } else {
        // Nighttime: Moon is active
        m_sunIntensity = 0.0f;

        // Moon is always dim (25% of sun's brightness)
        // In the future, we could vary moon intensity by lunar phase
        m_moonIntensity = 0.25f;
    }
}

bool SunTracker::shouldRecalculateLighting() const {
    // Calculate absolute difference between current and last recalculation angle
    float angleDiff = std::abs(m_sunAngle - m_lastRecalculationAngle);

    // Handle wrap-around case: 359° -> 1° should be 2°, not 358°
    if (angleDiff > 180.0f) {
        angleDiff = 360.0f - angleDiff;
    }

    // Trigger recalculation if difference exceeds threshold
    return angleDiff >= RECALC_THRESHOLD;
}

void SunTracker::resetRecalculationFlag() {
    // Mark current angle as the reference point for future recalculations
    m_lastRecalculationAngle = m_sunAngle;
}
