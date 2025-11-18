/**
 * @file sun_tracker.h
 * @brief Sun/moon position tracking for viewport-based dynamic lighting
 */

#pragma once

#include <glm/glm.hpp>

/**
 * @brief Tracks sun/moon position and triggers lighting recalculation
 *
 * The SunTracker monitors the sun's position in the sky and determines when
 * lighting should be recalculated based on significant angle changes.
 *
 * In the viewport-based lighting system, we only recalculate chunk lighting
 * when the sun/moon has moved enough to cause noticeable visual changes.
 * This prevents unnecessary recalculations while maintaining dynamic lighting.
 */
class SunTracker {
public:
    SunTracker();

    /**
     * @brief Update sun position based on time of day
     *
     * Calculates the current sun angle, direction vector, and intensities
     * for both sun and moon based on the current time of day.
     *
     * @param deltaTime Frame time in seconds (currently unused, for future use)
     * @param timeOfDay Current time (0.0-1.0 range, where 0=midnight, 0.5=noon, 1.0=midnight)
     */
    void update(float deltaTime, float timeOfDay);

    /**
     * @brief Check if lighting should be recalculated
     *
     * Returns true when the sun angle has changed by more than the threshold
     * since the last lighting recalculation. This prevents recalculating
     * lighting every frame while still maintaining dynamic lighting.
     *
     * @return True if sun angle has changed >= RECALC_THRESHOLD degrees
     */
    bool shouldRecalculateLighting() const;

    /**
     * @brief Reset the recalculation flag
     *
     * Call this after recalculating lighting to mark the current sun angle
     * as the reference point for future recalculation checks.
     */
    void resetRecalculationFlag();

    // Getters for current sun/moon state

    /**
     * @brief Get current sun angle in degrees
     * @return Sun angle (0-360 degrees, where 0 = noon, 180 = midnight)
     */
    float getSunAngle() const { return m_sunAngle; }

    /**
     * @brief Get current sun intensity
     * @return Sun brightness (0.0-1.0, where 1.0 = peak brightness at noon)
     */
    float getSunIntensity() const { return m_sunIntensity; }

    /**
     * @brief Get current moon intensity
     * @return Moon brightness (0.0-1.0, constant at 0.25 during night)
     */
    float getMoonIntensity() const { return m_moonIntensity; }

    /**
     * @brief Get normalized sun direction vector
     * @return Sun direction in world space (unit vector)
     */
    glm::vec3 getSunDirection() const { return m_sunDirection; }

private:
    float m_sunAngle;                  ///< Current sun angle (0-360 degrees)
    float m_lastRecalculationAngle;    ///< Sun angle at last lighting recalculation
    float m_sunIntensity;              ///< Current sun brightness (0.0-1.0)
    float m_moonIntensity;             ///< Current moon brightness (0.0-1.0)
    glm::vec3 m_sunDirection;          ///< Normalized sun direction vector

    /// Recalculate lighting when sun moves >= 15 degrees
    /// This provides ~24 lighting updates per full day/night cycle (360/15)
    /// Balances visual quality vs performance
    static constexpr float RECALC_THRESHOLD = 15.0f;
};
