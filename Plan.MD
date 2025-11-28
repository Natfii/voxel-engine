diff --git a/Plan.md b/Plan.md
new file mode 100644
index 0000000000000000000000000000000000000000..af5bb21c87801a9af23c58724ca77f0474ef10c6
--- /dev/null
+++ b/Plan.md
@@ -0,0 +1,510 @@
+# 3D Skeletal Annotation Tool Plan
+
+## Goals
+- Load `.glb` / `.gltf` character assets (e.g., player model in `assets/models`).
+- Render the mesh in a manipulable 3D viewport, allowing free orbit/pan/zoom and WASD fly.
+- Let users draw a simple animation-ready skeleton: spine, legs (L/R), arms (L/R), head; optional tail near hips.
+- Save annotated skeleton data for the engine to drive leg movement, tail swing, and head bobbing.
+- Launchable from the in-game console via the command `3deditor`, opening the tool in a new window.
+
+## User Experience & Controls
+- **Viewport navigation**
+  - Mouse: left-drag = orbit, right-drag = pan, scroll = zoom.
+  - Keyboard: WASD = fly, Q/E = vertical strafe, Shift = fast.
+  - Reset view button: focus model bounds.
+- **Skeleton creation flow**
+  1. Load model (auto-load selected from console or file picker rooted at `assets/models`).
+  2. Wizard prompts to place bones in order: spine root → spine tip → left leg → right leg → left arm → right arm → head. Ask: "Does the character have a tail?" If yes, add tail base + tip near hips.
+  3. Each placement uses a gizmo (translate widget) snapping to surface/vertex. Support undo/redo.
+  4. Preview procedural animations: walk cycle leg swing, idle head bob, tail sway using annotated joints.
+- **Saving**
+  - Save to `assets/rigs/<model_name>.json` (or `.yaml`) containing bone positions/parents in model space plus optional metadata (tail flag, model file reference).
+
+## Data Model
+```jsonc
+{
+  "model": "assets/models/player.glb",
+  "bones": [
+    {"name": "spine_root", "parent": null, "position": [x,y,z]},
+    {"name": "spine_tip",  "parent": "spine_root", "position": [x,y,z]},
+    {"name": "leg_L",      "parent": "spine_root", "position": [x,y,z]},
+    {"name": "leg_R",      "parent": "spine_root", "position": [x,y,z]},
+    {"name": "arm_L",      "parent": "spine_tip",  "position": [x,y,z]},
+    {"name": "arm_R",      "parent": "spine_tip",  "position": [x,y,z]},
+    {"name": "head",       "parent": "spine_tip",  "position": [x,y,z]},
+    {"name": "tail_base",  "parent": "spine_root", "position": [x,y,z], "optional": true},
+    {"name": "tail_tip",   "parent": "tail_base",  "position": [x,y,z], "optional": true}
+  ],
+  "version": 1
+}
+```
+
+## Open-Source Library Options
+- **cgltf** (MIT): tiny C parser for glTF 2.0; easy to embed, no heavy dependencies.
+- **tinygltf** (MIT): header-only C++ glTF loader supporting `.glb` & `.gltf` + buffers/textures.
+- **Assimp** (BSD): broad format support; heavier but handles glTF and scene hierarchy.
+- **Rendering**: continue with existing renderer (OpenGL/Vulkan?) and integrate mesh upload; or use **bgfx**/**glfw** if the tool is separable.
+- **UI**: **Dear ImGui** (already present? check `imgui.ini`), good for gizmos and docking.
+- **Gizmos**: **ImGuizmo** (MIT) for translate/rotate handles; works with ImGui.
+
+## Architecture
+- **Entry point**: console command `3deditor` triggers tool startup in a separate window context (new renderer + ImGui frame loop). Keep game loop running; communicate via command queue.
+- **Subsystems**
+  - `ModelLoader`: wraps chosen glTF loader, outputs mesh buffers + node transforms.
+  - `ViewportRenderer`: handles camera controls, lighting, and draw calls (mesh + skeleton overlays).
+  - `SkeletonEditorState`: tracks bone placements, selection, undo/redo stack.
+  - `Persistence`: read/write rig JSON files under `assets/rigs`.
+  - `PreviewAnimator`: simple procedural animations using placed bones.
+- **Data flow**
+  1. `3deditor` command passes target model path (or opens picker).
+  2. `ModelLoader` loads mesh and default pose; compute bounding box for camera framing.
+  3. `SkeletonEditorState` initializes expected bones; tail nodes created only if user confirms.
+  4. `ViewportRenderer` draws mesh; overlay bone gizmos and links.
+  5. On save, `Persistence` writes rig file; engine loads it at runtime to drive animation.
+
+## Rendering & Gizmo Snippets (illustrative)
+```cpp
+// Load glTF
+Model model = ModelLoader::Load("assets/models/player.glb");
+
+// Camera update
+camera.UpdateOrbit(input.mouseDelta, input.scroll, input.keyWASD);
+
+// Draw
+renderer.BeginFrame();
+renderer.DrawMesh(model);
+for (auto& bone : bones) {
+    renderer.DrawGizmo(bone.position, selected == bone);
+}
+renderer.EndFrame();
+```
+
+```cpp
+// Saving
+Rig rig = BuildRigFromBones(bones, modelPath);
+SaveRigJson("assets/rigs/player.json", rig);
+```
+
+## Concrete Code Examples (reference-ready)
+
+The following snippets are designed to be drop-in starting points using widely adopted libraries. Adjust namespaces/paths to
+match the engine codebase.
+
+### tinygltf mesh upload (OpenGL)
+```cpp
+#include <tiny_gltf.h>
+
+Model ModelLoader::Load(const std::string& path) {
+    tinygltf::TinyGLTF loader;
+    tinygltf::Model gltf;
+    std::string err, warn;
+
+    bool ok = loader.LoadBinaryFromFile(&gltf, &err, &warn, path); // also supports LoadASCIIFromFile
+    if (!warn.empty()) { std::cerr << "tinygltf warning: " << warn << "\n"; }
+    if (!ok) { throw std::runtime_error("Failed to load glTF: " + err); }
+
+    Model model;
+    for (const auto& mesh : gltf.meshes) {
+        for (const auto& primitive : mesh.primitives) {
+            const auto& posAccessor = gltf.accessors[primitive.attributes.at("POSITION")];
+            const auto& posView = gltf.bufferViews[posAccessor.bufferView];
+            const auto& buf = gltf.buffers[posView.buffer];
+            const unsigned char* data = buf.data.data() + posView.byteOffset + posAccessor.byteOffset;
+
+            GLuint vbo = 0; glGenBuffers(1, &vbo);
+            glBindBuffer(GL_ARRAY_BUFFER, vbo);
+            glBufferData(GL_ARRAY_BUFFER, posAccessor.count * sizeof(glm::vec3), data, GL_STATIC_DRAW);
+            model.meshes.push_back({vbo, posAccessor.count});
+        }
+    }
+    return model;
+}
+```
+
+### ImGui + ImGuizmo translate handle
+```cpp
+// Inside the editor frame loop
+ImGui::Begin("Rig Editor");
+ImGuizmo::BeginFrame();
+
+ImGuizmo::SetOrthographic(false);
+ImGuizmo::SetDrawlist();
+ImGuizmo::SetRect(viewport.x, viewport.y, viewport.w, viewport.h);
+
+glm::mat4 view = camera.ViewMatrix();
+glm::mat4 proj = camera.ProjectionMatrix();
+glm::mat4 model = glm::translate(glm::mat4(1.0f), selectedBone.position);
+
+ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
+                     ImGuizmo::TRANSLATE, ImGuizmo::LOCAL,
+                     glm::value_ptr(model));
+
+if (ImGuizmo::IsUsing()) {
+    selectedBone.position = glm::vec3(model[3]); // extract translation
+}
+ImGui::End();
+```
+
+### Rig file serialization (JSON, nlohmann::json)
+```cpp
+#include <nlohmann/json.hpp>
+
+void SaveRigJson(const std::string& path, const Rig& rig) {
+    nlohmann::json j;
+    j["model"] = rig.modelPath;
+    j["version"] = 1;
+    for (const auto& b : rig.bones) {
+        j["bones"].push_back({
+            {"name", b.name},
+            {"parent", b.parent.empty() ? nullptr : nlohmann::json(b.parent)},
+            {"position", {b.pos.x, b.pos.y, b.pos.z}},
+            {"optional", b.optional}
+        });
+    }
+    std::ofstream out(path);
+    out << std::setw(2) << j;
+}
+```
+
+### Console binding to launch the editor
+```cpp
+// Pseudocode for console command registration
+console.RegisterCommand("3deditor", [](const std::vector<std::string>& args) {
+    std::string model = args.empty() ? "assets/models/player.glb" : args[0];
+    EditorRequest req{.type = EditorRequest::OpenModel, .path = model};
+    EditorService::Get().Post(req); // async to avoid blocking game loop
+});
+```
+
+## Skeleton Placement Logic
+1. Start with `spine_root` at model origin or pelvis guess (use bounding box center).
+2. `spine_tip` placed along +Y; length = 25–35% of model height.
+3. Legs anchored at hips (offset on ±X, downwards along −Y).
+4. Arms anchored at shoulders (offset ±X at spine tip height).
+5. Head on top of spine tip (offset +Y).
+6. If tail: base slightly behind spine root (−Z), tip extends backward/down.
+7. Allow users to drag gizmos to refine; provide numeric input fields.
+
+## File I/O & Compatibility
+- Store rig paths relative to project root for portability.
+- Include version field for future schema changes.
+- On load, validate bone order and required nodes; prompt to reassign missing optional bones.
+
+## Integration with Engine
+- Add console binding: when user types `3deditor [model]`, spawn the tool window.
+- Share resource loader to avoid duplicate texture/shader compilation.
+- Expose rig data to animation system: leg swing uses leg bones, head bob uses head, tail sway uses tail bones if present.
+
+## Optimization & Quality Ideas
+- **Faster asset iteration**
+  - Cache parsed glTF buffers/textures; add a file-watch reload so edits in DCC tools are reflected without restarting the tool.
+  - Optional GPU buffer compaction: reuse a staging VBO/IBO pool and defragment on model reload to limit driver allocations.
+- **Camera + framing polish**
+  - Auto-frame on load by fitting the bounding sphere; provide a "Frame Selection" that focuses on the active bone.
+  - Add damping / inertia to orbit to reduce jitter during precise bone placement.
+- **Gizmo usability**
+  - Provide axis locking (X/Y/Z hotkeys) and small step snapping (e.g., 0.01m) for fine tuning.
+  - Allow numeric entry per axis in a side panel; show world vs. model space coordinates.
+- **Rig heuristics & validation**
+  - Detect model up-axis (Y vs. Z) from node transforms; flip defaults accordingly.
+  - On save, validate required bones and flag extreme offsets (e.g., head too far from spine tip) with inline warnings.
+- **Animation preview fidelity**
+  - Precompute simple IK for legs to keep feet on the ground plane in the walk preview.
+  - Tail sway preview: damped sinusoid driven by a configurable frequency/amplitude exposed in the UI.
+- **Engine hookup ergonomics**
+  - Generate a small C++ header from the rig JSON (constexpr bone indices) to avoid string lookups in runtime code.
+  - Provide a CLI (`./3deditor_cli --bake assets/models/wolf.glb`) that produces a rig JSON without UI for CI pipelines.
+- **Performance profiling hooks**
+  - Integrate GPU timer queries around mesh draw + gizmo pass; expose in an ImGui overlay.
+  - Track CPU frame time histogram; flag frames over budget when gizmos are active.
+
+### Example: auto-framing camera from bounds (glm)
+```cpp
+void Camera::FrameBounds(const AABB& bounds) {
+    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
+    const float radius = glm::length(bounds.max - center);
+    // Assuming perspective camera with vertical fov
+    const float distance = radius / std::sin(fov * 0.5f) + radius;
+    position = center + distance * GetBackward();
+    target = center;
+    UpdateMatrices();
+}
+```
+
+### Example: rig validation checklist (pseudocode)
+```cpp
+ValidationResult ValidateRig(const Rig& rig) {
+    ValidationResult result;
+    auto require = [&](std::string_view name) {
+        if (!rig.HasBone(name)) result.errors.push_back("Missing " + std::string(name));
+    };
+    require("spine_root");
+    require("spine_tip");
+    require("leg_L");
+    require("leg_R");
+    require("arm_L");
+    require("arm_R");
+    require("head");
+    if (rig.HasBone("tail_tip") && !rig.HasBone("tail_base")) {
+        result.errors.push_back("tail_tip without tail_base");
+    }
+    // Simple sanity: head should be above spine_root in model space
+    if (auto head = rig.Find("head"); head && head->pos.y < rig.Find("spine_root")->pos.y) {
+        result.warnings.push_back("Head appears below spine root; check up-axis");
+    }
+    return result;
+}
+```
+
+## Testing Plan
+- Unit: rig JSON read/write round-trip; bone ordering validation.
+- Integration: load representative `.glb` (humanoid, quadruped with tail) and verify gizmo placement + save/load.
+- UX: ensure orbit/pan/zoom feel right; undo/redo works; tail prompt appears.
+- Performance: confirm editor window doesn’t stall main game loop.
+
+## Open Questions / Research Follow-ups
+- Confirm existing rendering stack (OpenGL? Vulkan? bgfx?) to choose compatible glTF loader.
+- Check if ImGui already integrated; if not, decide on docking backend.
+- Determine whether engine uses left-handed or right-handed coordinate system; adjust bone export accordingly.
+- Decide on animation retargeting needs (are these rigs per-model or standardized?).
+
+## References & Useful Links
+- glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.pdf
+- cgltf: https://github.com/jkuhlmann/cgltf
+- tinygltf: https://github.com/syoyo/tinygltf
+- ImGuizmo: https://github.com/CedricGuillemet/ImGuizmo
+
+---
+
+# 2D Particle Effect Editor Plan
+
+## Goals
+- Provide a drop-in, optimized 2D particle effect editor launched via the in-game console command `particaleditor` (spelling preserved per request) that opens in a new window.
+- Allow frame-by-frame authoring and preview of particle systems (e.g., muzzle flashes, explosions, smoke, debris) targeting the engine’s 2D renderer.
+- Support importing sprite sheets/atlases from `assets/textures/` and exporting particle definitions to `assets/particles/<name>.json` (or `.yaml`) for runtime playback.
+- Offer fast iteration: hot-reload of textures and live parameter updates without restarting the game.
+
+## User Experience & Controls
+- **Viewport**: 2D orthographic canvas with zoom/pan (mouse wheel zoom, middle-drag pan). Grid + rulers toggle; background color picker.
+- **Timeline**: optional frame-by-frame scrubber for hand-authored sequences (e.g., staged bursts). Keyframes per property (spawn rate, angle, velocity, color over lifetime) with interpolation curves.
+- **Emitter presets**: point, cone, area/box, circle; allow burst mode and continuous emission.
+- **Layered systems**: multiple emitters per effect (e.g., spark layer + smoke layer). Reorder, solo, mute, duplicate.
+- **Playback controls**: play/pause, loop, restart, step frame; FPS override for testing fixed-step playback.
+- **Gizmos**: on-canvas handles to rotate/scale emitter shapes; ImGui sliders for numeric parameters; undo/redo.
+- **Snapping & alignment**: snap emitter origin to pixel grid; show crosshair for anchor alignment.
+- **Export/Import**: save/load effect definitions; auto-preview loaded effect when selected.
+
+## Data Model (example JSON)
+```jsonc
+{
+  "name": "muzzle_flash",
+  "version": 1,
+  "emitters": [
+    {
+      "type": "cone", // point | cone | box | circle
+      "duration": 0.1,
+      "loop": false,
+      "burst": { "count": 24, "cycles": 1 },
+      "rate": { "start": 0, "end": 0 },
+      "angle": { "min": -10, "max": 10 },
+      "speed": { "min": 12, "max": 18 },
+      "gravity": [0, -30],
+      "lifetime": { "min": 0.08, "max": 0.16 },
+      "size": { "start": [24, 12], "end": [0, 0], "curve": "ease_out" },
+      "color": { "gradient": [[0.0, "#FFE7A0"], [0.4, "#FF9B2F"], [1.0, "#00000000"]] },
+      "texture": { "atlas": "assets/textures/vfx_sheet.png", "frame": 5, "blend": "additive" },
+      "align_to_velocity": true,
+      "seed": 1337
+    }
+  ]
+}
+```
+
+## Open-Source Library Options (2D-centric)
+- **UI**: reuse Dear ImGui for panels; **ImPlot** (MIT) for curves (color/size over lifetime) and timeline visualization.
+- **Rendering**: engine’s existing 2D renderer; otherwise **bgfx**, **SDL + OpenGL**, or **raylib** (if a quick standalone is acceptable). Batch sprites for minimal draw calls.
+- **Serialization**: **nlohmann::json** for portability; **yaml-cpp** if matching prior assets.
+- **Hot-reload**: **fswatch** / platform file watcher for texture + particle definition reloads.
+
+## Architecture
+- **Entry point**: console command `particaleditor [effect_name]` posts a request to spawn the editor window; optionally preload the selected effect.
+- **Subsystems**
+  - `ParticleEditorState`: holds emitters, timeline keyframes, selection, undo/redo.
+  - `ParticleSystemCompiler`: resolves emitter definitions into runtime-friendly structs/buffers for fast playback.
+  - `ParticleRenderer2D`: draws particles with batching, sorting by blend mode (opaque/additive/alpha).
+  - `Persistence`: read/write particle JSON; handles relative paths to textures.
+  - `PlaybackController`: handles play/pause, loop, frame stepping, and FPS override.
+- **Data flow**
+  1. `particaleditor` command opens the window and loads the last-used or provided effect.
+  2. User edits emitter params; `ParticleSystemCompiler` rebuilds a runtime instance live.
+  3. `ParticleRenderer2D` renders preview with deterministic seeding for reproducible playback.
+  4. Save writes JSON to `assets/particles/<name>.json`; engine loads on level start.
+
+## Key Features & Implementation Notes
+- **Frame-by-frame timeline**: each emitter channel supports keyframes (rate, angle, speed, color, size, drag) with linear or cubic interpolation. Provide curve editor using ImPlot; show onion-skin of previous/next frames for hand-animated sprites.
+- **Sprite sheet/atlas support**: frame picker UI with visual thumbnails; support flipbook animation per particle (frame range + FPS). Add option to randomize start frame.
+- **Emitter shapes**: provide runtime shape sampling utilities (cone angle/box bounds/circle radius). Visualize shapes in the viewport.
+- **Blend modes**: additive, alpha, premultiplied alpha. Show over black/white checker backgrounds to judge artifacts.
+- **Sorting**: stable sorting per blend bucket; allow Z-order override for layered effects.
+- **Anchoring**: anchor point per effect (e.g., muzzle offset) for easy placement in-game. Allow positional offsets and rotation relative to anchor.
+- **Profiling/optimization hooks**: show particle count, draw calls, CPU ms/frame, GPU ms/frame (timer queries). Add budget hints (e.g., warn if >5k particles).
+- **Randomness**: expose seed in UI; deterministic mode for testing; stochastic mode for variation.
+- **Hot-reload**: watch texture + JSON files; auto-refresh preview. Display toast when reload occurs.
+- **Validation**: on save, ensure textures exist, frame indices in range, non-negative lifetimes, and at least one emitter.
+
+## Example Snippets (reference-ready)
+
+### Console binding to launch particle editor
+```cpp
+console.RegisterCommand("particaleditor", [](const std::vector<std::string>& args) {
+    std::string effect = args.empty() ? "assets/particles/example.json" : args[0];
+    ParticleEditorRequest req{.type = ParticleEditorRequest::Open, .path = effect};
+    ParticleEditorService::Get().Post(req);
+});
+```
+
+### Particle definition serialization (nlohmann::json)
+```cpp
+void SaveParticleJson(const std::string& path, const ParticleEffect& fx) {
+    nlohmann::json j;
+    j["name"] = fx.name;
+    j["version"] = 1;
+    for (const auto& e : fx.emitters) {
+        j["emitters"].push_back({
+            {"type", e.type},
+            {"duration", e.duration},
+            {"loop", e.loop},
+            {"burst", {{"count", e.burst.count}, {"cycles", e.burst.cycles}}},
+            {"rate", {{"start", e.rate.start}, {"end", e.rate.end}}},
+            {"angle", {{"min", e.angle.min}, {"max", e.angle.max}}},
+            {"speed", {{"min", e.speed.min}, {"max", e.speed.max}}},
+            {"gravity", {e.gravity.x, e.gravity.y}},
+            {"lifetime", {{"min", e.lifetime.min}, {"max", e.lifetime.max}}},
+            {"size", {{"start", {e.size.start.x, e.size.start.y}}, {"end", {e.size.end.x, e.size.end.y}}, {"curve", e.size.curve}}},
+            {"color", {{"gradient", e.color.gradient}}},
+            {"texture", {{"atlas", e.texture.atlas}, {"frame", e.texture.frame}, {"blend", e.texture.blend}}},
+            {"align_to_velocity", e.alignToVelocity},
+            {"seed", e.seed}
+        });
+    }
+    std::ofstream out(path);
+    out << std::setw(2) << j;
+}
+```
+
+### ImGui timeline sketch (pseudo-C++)
+```cpp
+ImGui::Begin("Timeline");
+float fps = state.timelineFps;
+ImGui::SliderFloat("FPS", &fps, 5.0f, 120.0f, "%.0f");
+state.timelineFps = fps;
+
+if (ImPlot::BeginPlot("##rate_curve", "time", "rate")) {
+    auto& keys = selectedEmitter.rateKeys; // vector<CurveKey>
+    std::vector<double> xs, ys;
+    for (auto& k : keys) { xs.push_back(k.time); ys.push_back(k.value); }
+    ImPlot::PlotLine("rate", xs.data(), ys.data(), (int)xs.size());
+    // handle key selection/drag to edit
+    ImPlot::EndPlot();
+}
+ImGui::End();
+```
+
+### Runtime particle update/render loop (simplified)
+```cpp
+void ParticleSystem::Update(float dt) {
+    for (auto& emitter : emitters) {
+        emitter.time += dt;
+        // spawn logic (burst or continuous)
+        SpawnParticles(emitter, dt);
+    }
+    for (auto& p : particles) {
+        p.age += dt;
+        if (p.age > p.lifetime) { p.alive = false; continue; }
+        float t = p.age / p.lifetime;
+        p.position += p.velocity * dt + 0.5f * p.gravity * dt * dt;
+        p.velocity += p.gravity * dt;
+        p.size = Lerp(p.sizeStart, p.sizeEnd, SizeCurve(t));
+        p.color = ColorGradient(t);
+    }
+    CompactDeadParticles();
+}
+
+void ParticleSystem::Render(ParticleRenderer2D& renderer) {
+    renderer.Begin(batchConfig);
+    for (auto& p : particles) {
+        if (!p.alive) continue;
+        renderer.Draw(p.textureHandle, p.position, p.size, p.rotation, p.color, p.blendMode);
+    }
+    renderer.End();
+}
+```
+
+## Performance & Optimization Ideas
+- Batch particles by texture + blend mode to minimize state changes; pre-sort draw lists.
+- Use a ring buffer / arena allocator for particle instances to avoid per-frame heap churn; reuse memory across playbacks.
+- SIMD-friendly particle updates (SoA layout) if profiling shows CPU bottlenecks; consider compute shader path for GPU-heavy scenes.
+- Optional fixed-pool cap per emitter to avoid runaway counts; display warning when reaching limits.
+- Allow downscaling preview resolution for slow machines; expose VSync toggle.
+
+## Testing Plan
+- Unit: JSON read/write round-trip for particle definitions; validation for required fields and frame indices.
+- Integration: load representative effects (smoke, muzzle flash, explosion) and verify timeline playback + hot-reload.
+- UX: confirm gizmo manipulation, keyframe editing, and onion-skinning work; ensure deterministic playback when seed is fixed.
+- Performance: profile particle count vs. frame time; verify batching reduces draw calls.
+
+## Open Questions / Research Follow-ups
+- Confirm engine 2D renderer API (sprite batching, blend modes, atlas format) to align data schema.
+- Decide on color space (sRGB vs. linear) and premultiplied alpha expectations for textures.
+- Determine if the editor should support physics-driven particles (collisions) or stay purely kinematic.
+- Evaluate whether to add sound triggers per emitter (e.g., small pops on burst start).
+
+## Useful References
+- ImPlot: https://github.com/epezent/implot
+- JSON for Modern C++: https://github.com/nlohmann/json
+- Particle system deep dive (GDC): https://www.gdcvault.com/play/1022970/Low-Overhead-High-Performance-Particle
+
+## For AI Assistants: Multi-Agent Execution Prompt (5 Agents)
+
+### Goal
+Orchestrate five specialized agents to design and implement both tools (3D skeletal annotation editor via `3deditor` and 2D particle effect editor via `particaleditor`) as optimized, drop-in modules for the engine. Agents must respect existing architecture, keep diffs minimal and well-documented, and surface open questions when assumptions are needed.
+
+### Recommended Agent Roster
+1. **Planner/PM Agent** — decomposes tasks, sequences milestones, tracks scope, and curates TODOs/open questions.
+2. **Engine Integration Agent** — owns engine-facing code paths (console commands, window/context creation, renderer hooks, asset I/O paths, build config updates).
+3. **Rendering & UI Agent** — builds viewport UI/UX (ImGui/ImGuizmo/ImPlot), camera controls, gizmos, timeline, and visual widgets.
+4. **Data & Persistence Agent** — defines schemas (rig JSON, particle JSON), implements serialization/deserialization, validation, and migration/versioning.
+5. **Performance & QA Agent** — profiles hotspots, recommends memory layouts/batching, adds assertions/tests, and documents runbooks.
+
+### Example Master Prompt (for the Planner/PM Agent)
+```
+You are coordinating 5 agents to deliver two editor tools: 3D skeletal annotation (`3deditor`) and 2D particle editor (`particaleditor`).
+- Maintain a shared task board with owners (Integration, Rendering/UI, Data/Persistence, Perf/QA).
+- Enforce coding guidelines: no try/catch around imports; prefer small, reviewable commits; cite sources/paths in notes.
+- Keep diffs minimal and focused; preserve existing engine style.
+- Call out open questions (renderer API, asset paths, build flags) and propose defaults with toggles.
+Deliverables: merged code with buildable state, updated docs/config, and smoke tests for both tools.
+```
+
+### Coordination Plan
+- **Kickoff**: Planner drafts milestones and assigns tasks; Integration validates console command wiring (`3deditor`, `particaleditor`) and window/context bootstrapping.
+- **Skeleton Tool Pass**: Rendering/UI implements viewport + gizmos; Data Agent finalizes rig schema + persistence; Perf/QA adds profiling hooks and validation.
+- **Particle Tool Pass**: Rendering/UI builds timeline & preview; Data Agent finalizes emitter/curve schema; Integration wires hot-reload; Perf/QA benchmarks batching and memory pools.
+- **Convergence**: Planner merges branches, ensures consistent naming/paths (`assets/models`, `assets/rigs`, `assets/particles`), and coordinates test runs.
+
+### Minimal Task Board Template
+- Skeleton editor: console launch, glTF load & bounds framing, gizmo placement, rig save/load, preview anims, validations.
+- Particle editor: console launch, emitter params, curves/timelines, spawn/burst playback, JSON I/O, batching.
+- Cross-cutting: hot-reload, config flags, docs/readme updates, profiling and QA smoke tests.
+
+### Communication & Handoff Protocol
+- Each agent posts short design notes with file paths and function names before coding.
+- Prefer incremental PRs per tool; Planner rebases and resolves conflicts.
+- Use common helper utilities for JSON, math types, and rendering to avoid duplication.
+- Record test commands and profiling setups for reproducibility.
+
+### Testing & Quality Gate
+- Unit: JSON round-trips for rigs/particles; schema validation with version tags.
+- Integration: console commands open windows; load sample assets; verify gizmo edits and timeline playback.
+- Performance: measure frame time with 1k+ particles; verify batching reduces draw calls; validate camera framing speed for large meshes.
+- UX: mouse/keyboard controls, undo/redo, autosave prompts, and error surfacing in UI log.

---

# IMPLEMENTATION CHECKLIST (Based on Codebase Analysis)

**Analyzed:** 2025-11-26
**Engine Status:** Vulkan with mesh pipeline, ImGui 1.91.9b, glTF loading ready

---

## EXISTING INFRASTRUCTURE

| Component | Status | File |
|-----------|--------|------|
| Vulkan Renderer | Ready | `vulkan_renderer.h` |
| glTF Loading | Ready | `mesh_loader.h` - `loadGLTF()` |
| OBJ Loading | Ready | `mesh_loader.h` - `loadOBJ()` |
| ImGui | Ready | `external/imgui-1.91.9b` |
| Console System | Ready | `command_registry.h` |
| Particle System | Basic | `particle_system.h` (needs extension) |
| PBR Materials | Ready | `mesh.h` - `PBRMaterial` |

---

## DEPENDENCIES TO ADD

- [ ] **ImGuizmo** - Download to `external/ImGuizmo/`
  - URL: https://github.com/CedricGuillemet/ImGuizmo
  - Files: `ImGuizmo.h`, `ImGuizmo.cpp`

- [ ] **ImPlot** - Download to `external/implot/`
  - URL: https://github.com/epezent/implot
  - Files: `implot.h`, `implot.cpp`, `implot_items.cpp`, `implot_internal.h`

---

## PHASE 1: 3D SKELETAL EDITOR (`3deditor`)

### 1.1 Foundation (~800 lines)
- [ ] Create `include/editor/editor_window.h` - Base window class
- [ ] Create `src/editor/editor_window.cpp` - GLFW + Vulkan context
- [ ] Create `include/editor/editor_camera.h` - Orbit/pan/zoom camera
- [ ] Create `src/editor/editor_camera.cpp` - Camera implementation
- [ ] Add `3deditor` command to `console_commands.cpp`

### 1.2 Model Loading (~500 lines)
- [ ] Extend `mesh_loader.cpp` - Extract glTF node hierarchy
- [ ] Add `GLTFNode` and `GLTFScene` structs to `mesh_loader.h`
- [ ] Create `src/editor/editor_renderer.cpp` - Mesh + bone visualization

### 1.3 Skeleton Editor UI (~1200 lines)
- [ ] Add ImGuizmo to CMakeLists.txt
- [ ] Create `include/editor/skeleton_editor_state.h` - Bone data + undo/redo
- [ ] Create `src/editor/skeleton_editor_state.cpp` - State management
- [ ] Create `src/editor/skeleton_editor_ui.cpp` - ImGui panels
  - [ ] Hierarchy panel (bone tree)
  - [ ] Properties panel (position editing)
  - [ ] Wizard panel (step-by-step placement)
  - [ ] Toolbar (save/load/reset/preview)
- [ ] Integrate ImGuizmo for bone translation gizmos

### 1.4 Persistence & Preview (~600 lines)
- [ ] Create `include/editor/rig_file.h` - Rig file format
- [ ] Create `src/editor/rig_file.cpp` - YAML serialization
- [ ] Create `include/editor/preview_animator.h` - Animation preview
- [ ] Create `src/editor/preview_animator.cpp` - Walk/idle/tail animations
- [ ] Add validation for required bones

**Phase 1 Total: ~3100 lines**

---

## PHASE 2: 2D PARTICLE EDITOR (`particaleditor`)

### 2.1 Extended Particle System (~1500 lines)
- [ ] Create `include/particle/particle_effect.h` - New data structures
  - [ ] `EmitterShape` enum (point, cone, box, circle)
  - [ ] `EmitterConfig` struct (all emitter parameters)
  - [ ] `ParticleEffect` struct (multiple emitters)
- [ ] Create `include/particle/particle_emitter.h` - Emitter class
- [ ] Create `src/particle/particle_emitter.cpp` - Spawn/update logic
- [ ] Create `include/particle/particle_renderer.h` - GPU instanced rendering
- [ ] Create `src/particle/particle_renderer.cpp` - Batched particle drawing

### 2.2 Editor Foundation (~600 lines)
- [ ] Create `include/editor/particle_editor.h` - Editor main class
- [ ] Create `src/editor/particle_editor.cpp` - Editor implementation
- [ ] Create `src/editor/particle_viewport.cpp` - 2D orthographic view
- [ ] Add `particaleditor` command to `console_commands.cpp`

### 2.3 Editor UI (~1200 lines)
- [ ] Add ImPlot to CMakeLists.txt
- [ ] Create `src/editor/particle_emitter_panel.cpp` - Emitter properties
  - [ ] Shape selector
  - [ ] Numeric inputs
  - [ ] Color gradient editor
  - [ ] Texture picker
- [ ] Create `src/editor/curve_editor.cpp` - ImPlot-based curves
- [ ] Create `src/editor/particle_timeline.cpp` - Playback controls

### 2.4 Persistence & Polish (~500 lines)
- [ ] Create `src/editor/particle_file.cpp` - YAML serialization
- [ ] Create `src/editor/file_watcher.cpp` - Hot-reload support
- [ ] Add validation for texture paths, frame indices

**Phase 2 Total: ~3800 lines**

---

## CMAKE INTEGRATION

```cmake
# Add to CMakeLists.txt

# ImGuizmo
add_library(imguizmo STATIC external/ImGuizmo/ImGuizmo.cpp)
target_include_directories(imguizmo PUBLIC external/ImGuizmo)
target_link_libraries(imguizmo PRIVATE imgui)

# ImPlot
add_library(implot STATIC
    external/implot/implot.cpp
    external/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC external/implot)
target_link_libraries(implot PRIVATE imgui)

# Editor sources
set(EDITOR_SOURCES
    src/editor/editor_window.cpp
    src/editor/editor_camera.cpp
    src/editor/skeletal_editor.cpp
    src/editor/skeleton_editor_state.cpp
    src/editor/skeleton_editor_ui.cpp
    src/editor/rig_file.cpp
    src/editor/preview_animator.cpp
    src/editor/particle_editor.cpp
    src/editor/particle_viewport.cpp
    src/editor/particle_emitter_panel.cpp
    src/editor/curve_editor.cpp
    src/editor/particle_timeline.cpp
    src/editor/particle_file.cpp
    src/editor/file_watcher.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${EDITOR_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE imguizmo implot)
```

---

## QUICK START TASKS

1. [ ] Create directories: `include/editor/`, `src/editor/`, `include/particle/`
2. [ ] Download ImGuizmo to `external/ImGuizmo/`
3. [ ] Download ImPlot to `external/implot/`
4. [ ] Add placeholder commands (return "Not implemented yet")
5. [ ] Update CMakeLists.txt with new library targets

---

## TOTAL EFFORT

| Phase | Lines | Status |
|-------|-------|--------|
| Phase 1 (Skeletal) | ~3100 | Not Started |
| Phase 2 (Particle) | ~3800 | Not Started |
| **TOTAL** | ~6900 | - |
