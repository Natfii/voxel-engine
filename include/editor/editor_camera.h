/**
 * @file editor_camera.h
 * @brief Camera controller for the skeletal editor tool
 *
 * Provides orbit, pan, zoom, and fly controls for 3D model editing.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
 * @brief Camera controller optimized for 3D model editing
 *
 * The EditorCamera class implements multiple camera control modes suitable
 * for inspecting and manipulating 3D skeletal models:
 *
 * Camera Modes:
 * -------------
 * 1. Orbit Mode (Left-drag): Rotate camera around a target point
 *    - Maintains constant distance from target
 *    - Useful for inspecting models from all angles
 *    - Constrains pitch to prevent gimbal lock
 *
 * 2. Pan Mode (Right-drag): Move the camera target point
 *    - Translates camera and target together
 *    - Movement is in screen space (feels natural)
 *    - Maintains camera orientation
 *
 * 3. Zoom Mode (Scroll wheel): Adjust distance from target
 *    - Moves camera closer/farther from target
 *    - Exponential scaling for smooth feel
 *    - Clamped to prevent inverting through target
 *
 * 4. Fly Mode (WASD + QE): Free-flight navigation
 *    - WASD: Move forward/left/back/right
 *    - Q/E: Move down/up
 *    - Useful for navigating large scenes
 *    - Independent of target point
 *
 * Frame Bounds:
 * -------------
 * The frameBounds() method automatically positions the camera to view
 * a bounding box (e.g., model AABB) at optimal distance and angle.
 *
 * Coordinate System:
 * ------------------
 * - Y-up world space (Vulkan with Y-flip in projection)
 * - Right-handed coordinate system
 * - Position: Camera eye position in world space
 * - Target: Point the camera is looking at
 * - Distance: Separation between position and target
 *
 * Matrix Generation:
 * ------------------
 * - View Matrix: lookAt(position, target, up)
 * - Projection Matrix: perspective(fov, aspect, near, far)
 * - Projection includes Vulkan Y-flip: projection[1][1] *= -1
 *
 * Usage Example:
 * --------------
 * ```cpp
 * EditorCamera camera;
 * camera.frameBounds(modelMin, modelMax);  // Focus on model
 *
 * // In mouse callback:
 * if (leftButtonPressed) {
 *     camera.updateOrbit(deltaX, deltaY);
 * }
 * if (rightButtonPressed) {
 *     camera.updatePan(deltaX, deltaY);
 * }
 *
 * // In scroll callback:
 * camera.updateZoom(scrollDelta);
 *
 * // In update loop:
 * camera.updateFly(forward, right, up, deltaTime);
 *
 * // For rendering:
 * glm::mat4 view = camera.getViewMatrix();
 * glm::mat4 proj = camera.getProjectionMatrix(aspectRatio);
 * ```
 */
class EditorCamera {
public:
    /**
     * @brief Constructs an editor camera with default settings
     *
     * Default Configuration:
     * - Position: (5, 5, 5) - offset from origin
     * - Target: (0, 0, 0) - looking at origin
     * - Up: (0, 1, 0) - Y-axis up
     * - Distance: Calculated from position and target
     * - FOV: 45 degrees
     * - Near: 0.1 units
     * - Far: 1000.0 units
     */
    EditorCamera();

    /**
     * @brief Constructs an editor camera with custom parameters
     *
     * @param position Initial camera eye position
     * @param target Initial look-at target point
     * @param up World up vector (typically (0, 1, 0))
     */
    EditorCamera(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up);

    /**
     * @brief Updates camera rotation in orbit mode
     *
     * Rotates the camera around the target point while maintaining
     * constant distance. The camera orbits horizontally (yaw) and
     * vertically (pitch), similar to a turntable or trackball.
     *
     * Implementation Details:
     * - deltaX controls yaw (horizontal rotation)
     * - deltaY controls pitch (vertical rotation)
     * - Pitch is clamped to [-89°, +89°] to prevent gimbal lock
     * - Camera position is recalculated from spherical coordinates
     *
     * @param deltaX Horizontal mouse movement (pixels or normalized)
     * @param deltaY Vertical mouse movement (pixels or normalized)
     */
    void updateOrbit(float deltaX, float deltaY);

    /**
     * @brief Updates camera position in pan mode
     *
     * Translates both the camera and target point together in screen
     * space. The movement feels natural because it follows mouse motion:
     * - Right = camera moves right
     * - Up = camera moves up
     *
     * Implementation Details:
     * - Constructs right and up vectors from camera orientation
     * - Scales movement by distance (farther = bigger pan)
     * - Applies translation to both position and target
     *
     * @param deltaX Horizontal mouse movement (pixels or normalized)
     * @param deltaY Vertical mouse movement (pixels or normalized)
     */
    void updatePan(float deltaX, float deltaY);

    /**
     * @brief Updates camera distance in zoom mode
     *
     * Moves the camera closer to or farther from the target point
     * along the view direction. Uses exponential scaling for smooth
     * zooming across large distance ranges.
     *
     * Implementation Details:
     * - Positive delta = zoom in (decrease distance)
     * - Negative delta = zoom out (increase distance)
     * - Distance clamped to [minDistance, maxDistance]
     * - Position recalculated to maintain target
     *
     * @param delta Scroll wheel delta or zoom amount
     */
    void updateZoom(float delta);

    /**
     * @brief Sets the camera yaw angle directly
     * @param yaw Horizontal rotation in degrees
     */
    void setYaw(float yaw);

    /**
     * @brief Sets the camera pitch angle directly
     * @param pitch Vertical rotation in degrees
     */
    void setPitch(float pitch);

    /**
     * @brief Updates camera position in fly mode
     *
     * Free-flight navigation using WASD-style controls. Movement is
     * relative to camera orientation (not world axes), making it
     * intuitive for navigating 3D space.
     *
     * Movement Mapping:
     * - forward > 0: Move forward (W key)
     * - forward < 0: Move backward (S key)
     * - right > 0: Move right (D key)
     * - right < 0: Move left (A key)
     * - up > 0: Move up (E key)
     * - up < 0: Move down (Q key)
     *
     * Implementation Details:
     * - Calculates movement in camera-local space
     * - Scales by deltaTime for frame-rate independence
     * - Updates both position and target to maintain orientation
     *
     * @param forward Forward/backward movement amount
     * @param right Left/right movement amount
     * @param up Down/up movement amount
     * @param deltaTime Time elapsed since last frame (seconds)
     */
    void updateFly(float forward, float right, float up, float deltaTime);

    /**
     * @brief Automatically frames the camera to view a bounding box
     *
     * Calculates optimal camera position and distance to view the
     * entire bounding box. Useful for focusing on a loaded model
     * or selected geometry.
     *
     * Algorithm:
     * 1. Calculate bounding box center (becomes new target)
     * 2. Calculate bounding sphere radius
     * 3. Calculate distance needed to fit sphere in view frustum
     * 4. Position camera at (center + offset * distance)
     *
     * Default Viewing Angle:
     * - 45° from front (SW direction)
     * - 30° elevation (slightly above)
     *
     * @param min Minimum corner of bounding box
     * @param max Maximum corner of bounding box
     */
    void frameBounds(const glm::vec3& min, const glm::vec3& max);

    /**
     * @brief Gets the view matrix for rendering
     *
     * Constructs a view matrix using lookAt transformation.
     * The matrix transforms world space to camera/view space.
     *
     * @return View matrix (world → camera space)
     */
    glm::mat4 getViewMatrix() const;

    /**
     * @brief Gets the projection matrix for rendering
     *
     * Constructs a perspective projection matrix with Vulkan
     * coordinate system conventions:
     * - Y-axis flipped (projection[1][1] *= -1)
     * - Depth range [0, 1] instead of [-1, 1]
     *
     * @param aspectRatio Viewport width / height
     * @return Projection matrix (camera space → clip space)
     */
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    /**
     * @brief Gets the current camera position
     * @return Camera eye position in world space
     */
    glm::vec3 getPosition() const { return m_position; }

    /**
     * @brief Gets the current target point
     * @return Point the camera is looking at
     */
    glm::vec3 getTarget() const { return m_target; }

    /**
     * @brief Sets the camera target point
     * @param target New target point to orbit around
     */
    void setTarget(const glm::vec3& target) {
        m_target = target;
        updateCameraVectors();
    }

    /**
     * @brief Gets the current camera-to-target distance
     * @return Distance in world units
     */
    float getDistance() const { return m_distance; }

    /**
     * @brief Sets the camera distance from target
     * @param distance New distance (clamped to valid range)
     */
    void setDistance(float distance) {
        m_distance = glm::clamp(distance, MIN_DISTANCE, MAX_DISTANCE);
        updateCameraVectors();
    }

    /**
     * @brief Gets the current yaw angle
     * @return Yaw in degrees
     */
    float getYaw() const { return m_yaw; }

    /**
     * @brief Gets the current pitch angle
     * @return Pitch in degrees
     */
    float getPitch() const { return m_pitch; }

    /**
     * @brief Sets the field of view
     * @param fov Field of view in degrees (typically 30-90)
     */
    void setFOV(float fov) { m_fov = fov; }

    /**
     * @brief Sets the near clip plane distance
     * @param nearPlane Near clip distance (must be > 0)
     */
    void setNearPlane(float nearPlane) { m_near = nearPlane; }

    /**
     * @brief Sets the far clip plane distance
     * @param farPlane Far clip distance (must be > near)
     */
    void setFarPlane(float farPlane) { m_far = farPlane; }

private:
    // ========== Camera State ==========
    glm::vec3 m_position;    ///< Camera eye position in world space
    glm::vec3 m_target;      ///< Point the camera is looking at
    glm::vec3 m_up;          ///< Camera up vector (typically (0, 1, 0))

    float m_yaw;             ///< Horizontal rotation in degrees
    float m_pitch;           ///< Vertical rotation in degrees (clamped)
    float m_distance;        ///< Distance from camera to target

    // ========== Projection Parameters ==========
    float m_fov;             ///< Field of view in degrees
    float m_near;            ///< Near clip plane distance
    float m_far;             ///< Far clip plane distance

    // ========== Camera Control Settings ==========
    static constexpr float ORBIT_SENSITIVITY = 0.5f;      ///< Orbit rotation speed
    static constexpr float PAN_SENSITIVITY = 0.005f;      ///< Pan movement speed
    static constexpr float ZOOM_SENSITIVITY = 0.1f;       ///< Zoom speed factor
    static constexpr float FLY_SPEED = 5.0f;              ///< Fly mode movement speed
    static constexpr float MIN_DISTANCE = 0.5f;           ///< Minimum zoom distance
    static constexpr float MAX_DISTANCE = 1000.0f;        ///< Maximum zoom distance
    static constexpr float MIN_PITCH = -179.0f;           ///< Minimum pitch angle (full rotation)
    static constexpr float MAX_PITCH = 179.0f;            ///< Maximum pitch angle (full rotation)

    /**
     * @brief Recalculates camera position from spherical coordinates
     *
     * Converts (yaw, pitch, distance) to Cartesian position relative
     * to target point. Called after orbit or zoom operations.
     */
    void updateCameraVectors();
};
