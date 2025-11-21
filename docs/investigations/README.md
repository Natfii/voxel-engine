# Investigation Documents Archive

This directory contains investigation documents generated during debugging sessions. These documents were created by AI agents analyzing the codebase for performance issues and bugs.

## Status: Historical Reference Only

**All issues documented here have been fixed and integrated into the main codebase.**

The canonical documentation for all fixes is in `docs/ENGINE_HANDBOOK.md`. These files are kept for historical reference but are no longer actively maintained.

## Investigation Sessions

### Session 1: Lighting Performance Debug (Nov 21, 2025)
**Files:**
- `LIGHTING_INVESTIGATION_INDEX.md` - Master index of lighting issues
- `LIGHTING_QUEUE_ANALYSIS.md` - Light propagation queue analysis
- `LIGHTING_MEMORY_ANALYSIS.md` - Memory usage investigation
- `LIGHTING_PERFORMANCE_FIXES.md` - Proposed fix implementations
- `LIGHTING_PERFORMANCE_SUMMARY.txt` - Executive summary
- `LIGHTING_MEMORY_INVESTIGATION_INDEX.md` - Memory-focused index
- `LIGHTING_MEMORY_SUMMARY.txt` - Memory findings
- `LIGHTING_MEMORY_VISUAL_SUMMARY.txt` - Visual memory breakdown
- `LIGHTING_CODE_REFERENCES.txt` - Code location references
- `README_LIGHTING_INVESTIGATION.md` - Investigation overview

**Outcome:** Fixed lighting freeze (50+ sec), GPU stalls (900-1100ms), triple mesh generation, console spam, and more.

### Session 2: Voxel Math Optimization (Nov 21, 2025)
**Files:**
- `VOXEL_MATH_PERFORMANCE_ANALYSIS.md` - Coordinate conversion analysis
- `VOXEL_MATH_QUICK_REFERENCE.txt` - Optimization quick reference

**Outcome:** Replaced division with bit shifts (24-39x faster coordinate conversions).

### Session 3: Buffer Upload Analysis (Nov 21, 2025)
**Files:**
- `BUFFER_UPLOAD_ANALYSIS.md` - GPU buffer upload investigation

**Outcome:** Identified rate limiting and warm-up phase requirements.

### General
**Files:**
- `INVESTIGATION_SUMMARY.txt` - Overall findings summary

## Note on Agent Usage

These documents represent the output of **10+ parallel AI agents** analyzing different aspects of the codebase simultaneously. While thorough, this approach was acknowledged as "overkill" - a single focused investigation would have been more efficient.

**Lesson learned:** Use 2-3 targeted agents for specific problems rather than spawning 10+ agents in parallel.

## If You're Reading This

All the fixes from these investigations are now in production code. For current documentation, see:
- `docs/ENGINE_HANDBOOK.md` - Complete system documentation
- Git commit history - Detailed fix implementations
- Code comments - In-place explanations

These investigation documents can be safely deleted if needed - they're historical artifacts.
