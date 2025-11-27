# 2D Paint Editor Plan (64x64) — invoked via `painteditor`

## Goals
- Provide a lightweight, beginner-friendly 2D pixel paint tool embedded in the engine and opened from the in-game console with `painteditor`.
- Target a fixed 64x64 canvas (configurable later) with paint, fill, erase, pick-color, and selection/transform basics suited to sprite/icon creation.
- Support quick save/load to disk (e.g., `assets/paint/`), with file formats friendly to source control (PNG + sidecar JSON for metadata).
- Deliver an optimized, drop-in window using existing rendering stack (SDL2/ImGui/ImPlot-style docking) with minimal new dependencies.

## User stories
- As a creator, I can type `painteditor` in the console to pop up a windowed pixel editor without restarting the game.
- As a beginner, I can left-click to paint single pixels, right-click to erase, and use a fill bucket to flood regions.
- As an artist, I can undo/redo strokes, zoom/pan the canvas, and pick colors from pixels.
- As a reviewer, I can save/load sprites to compare revisions and export to PNG for sharing.
- As a designer, I can toggle grid lines and mirror drawing (X/Y) for symmetry.

## UX & controls
- Canvas: 64x64 grid with optional background checkerboard; zoom slider and scroll-wheel zoom; middle-drag pan.
- Tools: Brush (size 1–8), Eraser, Fill, Line, Rectangle (outline/fill), Circle (outline/fill), Color Picker (eyedropper), Select+Move, Mirror X/Y toggle, Replace Color, Text (bitmap font), and Clear.
- Color: Palette bar with presets + custom slots, HSV/RGB picker, alpha slider; recent colors strip.
- Shortcuts: `B` brush, `E` eraser, `F` fill, `G` grid toggle, `P` picker, `Z/Ctrl+Z` undo, `Y/Ctrl+Y` redo, `S/Ctrl+S` save, `O/Ctrl+O` open, `M` mirror toggle, `L` line, `R` rectangle, `C` circle, `Delete` clear selection.
- View: Grid toggle, checkerboard opacity slider, show cursor coords, status bar with tool/name/size, optional onion-skin for animation frames (future).
- Transparancy support 

## Data model & files
- Canvas: 64x64 RGBA8 buffer.
- Palette: List of up to 32 recent colors + 16 pinned slots (persisted per project in JSON sidecar).
- File formats:
  - Primary: PNG (lossless) with 64x64 constraint.
  - Metadata sidecar: JSON `{"name":"sprite","width":64,"height":64,"layers":[...],"palette":[...],"last_tool":"brush", "grid":true}` stored alongside PNG (e.g., `sprite.png` + `sprite.meta.json`).
  - Optional engine-native binary blob (RLE) for fast load.
- Undo/redo: Command stack capturing stroke operations and fill seeds; capped history to 256 actions.

## Architecture
- Entry: Console command `painteditor` registers a UI dock/window and opens the editor state.
- State: `PaintEditorState { canvas: TextureHandle, pixels: std::vector<uint32_t>, tool: enum Tool, palette, history, redo, selection }`.
- Systems:
  - Input: Map mouse/keyboard to tool actions; convert screen to grid coords with zoom/pan.
  - Drawing: CPU pixel ops (brush/fill/line) writing to `pixels`; GPU upload to texture each frame or dirty-rect updates.
  - UI: Built with ImGui + custom widgets (palette, toolbars, file dialog, status bar).
  - IO: Save/load PNG via stb_image_write / stb_image; JSON via nlohmann/json or cJSON.
  - Undo/redo: Command objects with `apply`/`revert` storing changed pixels.
- Performance: Dirty rectangles to minimize texture uploads; optional nearest-neighbor shader for crisp zoom; throttle saves to background thread.

## Feature checklist (beginner-friendly defaults)
- [ ] Brush/eraser with adjustable size and hardness (binary alpha for pixels).
- [ ] Fill bucket with 4/8-connect flood-fill and tolerance slider (RGBA diff).
- [ ] Color picker with hover preview and recent-color history.
- [ ] Line/rectangle/circle tools with shift for straight/locked aspect; outline vs filled toggle.
- [ ] Selection: click-drag marquee, move selection, copy/paste, flip/mirror selection.
- [ ] Mirror drawing mode (X and Y); optional tiled preview.
- [ ] Grid and checkerboard backgrounds; configurable grid color/alpha.
- [ ] Undo/redo stack with history panel.
- [ ] File: New, Open, Save, Save As, Export PNG; recent files menu.
- [ ] Basic layers (optional v1.1): 2–4 layers with opacity and merge-down.
- [ ] Frame-by-frame (future): simple timeline to add frames for animation previews.

## Integration hooks
- Console binding example (pseudo-C++ with ImGui):
  ```cpp
  register_console_command("painteditor", [](){ PaintEditor::Open(); });
  ```
- Editor loop skeleton:
  ```cpp
  void PaintEditor::Render() {
    ImGui::Begin("Paint Editor", &open);
    Toolbar(); Palette();
    CanvasView(); // handles zoom/pan/mouse painting
    StatusBar();
    ImGui::End();
  }
  ```

## Reference code snippets (research-oriented)
- Flood fill (4-way) on CPU with tolerance:
  ```cpp
  void flood_fill(Vec2i seed, uint32_t target, uint32_t replacement, int tol) {
    if (target == replacement) return;
    std::queue<Vec2i> q; q.push(seed);
    auto close = [&](uint32_t a, uint32_t b){
      int dr = ((a>>24)&0xFF) - ((b>>24)&0xFF);
      int dg = ((a>>16)&0xFF) - ((b>>16)&0xFF);
      int db = ((a>>8)&0xFF) - ((b>>8)&0xFF);
      int da = (a&0xFF) - (b&0xFF);
      return dr*dr+dg*dg+db*db+da*da <= tol*tol;
    };
    while(!q.empty()) {
      auto p = q.front(); q.pop();
      if (!in_bounds(p) || !close(pixels[idx(p)], target)) continue;
      pixels[idx(p)] = replacement;
      q.push({p.x+1,p.y}); q.push({p.x-1,p.y});
      q.push({p.x,p.y+1}); q.push({p.x,p.y-1});
    }
    mark_dirty();
  }
  ```
- Brush stamp (square) with dirty-rect tracking:
  ```cpp
  void stamp(Vec2i p, int radius, uint32_t color) {
    Rect dirty = {p.x-radius, p.y-radius, p.x+radius, p.y+radius};
    for (int y=-radius; y<=radius; ++y)
      for (int x=-radius; x<=radius; ++x)
        set_pixel(p.x+x, p.y+y, color, dirty);
    upload_dirty(dirty);
  }
  ```
- Uploading canvas to GPU texture (ImGui/GL example):
  ```cpp
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, dirty.x0, dirty.y0,
                  dirty.w(), dirty.h(), GL_RGBA, GL_UNSIGNED_BYTE,
                  pixels.data() + idx(dirty.x0, dirty.y0));
  ```
- Save PNG + JSON sidecar:
  ```cpp
  stbi_write_png((path+".png").c_str(), 64, 64, 4, pixels.data(), 64*4);
  json meta = { {"width",64}, {"height",64}, {"palette", palette}, {"grid", grid_enabled} };
  std::ofstream(path+".meta.json") << meta.dump(2);
  ```
- ImGui canvas interaction sketch (zoom/pan + pixel draw):
  ```cpp
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float zoom = state.zoom; // e.g., 8x default
  ImVec2 canvas_size = {64*zoom, 64*zoom};
  ImGui::InvisibleButton("canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
  ImVec2 origin = ImGui::GetItemRectMin();
  if (ImGui::IsItemHovered()) {
    ImVec2 m = ImGui::GetIO().MousePos;
    int gx = int((m.x - origin.x) / zoom);
    int gy = int((m.y - origin.y) / zoom);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) draw_pixel({gx,gy});
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) erase_pixel({gx,gy});
  }
  ImGui::SetCursorScreenPos(origin);
  ImGui::Image((ImTextureID)(intptr_t)tex, canvas_size);
  ```

## Optimization and UX polish ideas
- Snap-to-grid cursor preview; show pixel coordinate under cursor.
- Mirror guides displayed on canvas; crosshair cursor for accuracy.
- Autosave every N minutes to a temp path; crash recovery on next launch.
- Smooth zoom (Ctrl + mouse wheel) and double-click to reset zoom to fit.
- Optional checkerboard strength slider and dark/light UI theme toggle.
- High-DPI aware rendering; use nearest-neighbor sampling to keep pixels crisp.
- Limit history memory by storing sparse delta buffers; compress undo steps via RLE.
- Recent files + thumbnails in the Open dialog; drag PNG into window to load.
- Optional reference layer: import another image at low opacity for tracing.

## Testing strategy
- Unit: flood-fill tolerance, undo/redo correctness, PNG read/write roundtrip, mirror drawing consistency.
- Integration: console command opens editor; save->load preserves pixels and palette; dirty-rect uploads only update modified regions.
- Manual: verify shortcuts, grid toggle, mirror mode, and zoom/pan ergonomics on 1080p/4K.

## Open questions / follow-ups
- Should we support animation frames in v1 or keep single-frame and add timeline in v1.1?
- Where to store autosaves (`assets/paint/tmp/`)? How to prune old files?
- Do we need palette import/export (GPL palettes) or Aseprite palette compatibility?

---

# AI Assistants Prompt (5-agent system)
Use this prompt when coordinating AI agents to build the 2D paint editor.

## Roles
1) **Planner** — expand requirements, slice tasks, track scope creep.
2) **Renderer** — handle canvas rendering, zoom/pan, texture upload, grid/mirror visuals.
3) **Tools & IO** — implement brush/fill/selection tools, undo/redo, save/load PNG+JSON.
4) **UX & QA** — shortcuts, ergonomics, accessibility, testing matrix.
5) **Integrator** — console command wiring (`painteditor`), build flags, dependency checks.

## Collaboration protocol
- Start with a shared task board. Planner seeds tasks; others claim/append details.
- Communicate decisions in short, numbered updates. Ask for blockers explicitly.
- Keep code snippets minimal and engine-idiomatic (ImGui + GL/SDL examples preferred).
- Require Renderer + Tools & IO sign-off before shipping canvas interactions.

## Task board template
```
[Backlog]
- ...
[In Progress]
- ...
[Review]
- ...
[Done]
- ...
```

## Delivery checklist
- Console command `painteditor` opens the window.
- Brush/eraser/fill/picker work with undo/redo and grid toggle.
- Save/load PNG+meta JSON roundtrip validated.
- Mirror draw and zoom/pan verified.
- Docs: shortcuts, file locations, and known limits noted.