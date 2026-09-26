# Non-Planar Ironing Design Document

## Project Overview

This document describes the architecture and design for implementing non-planar ironing in OrcaSlicer. The feature aims to investigate whether conventional FDM printers can achieve smoother surfaces by using controlled non-planar ironing within the mechanical constraints of standard toolheads.

## Current OrcaSlicer Ironing Architecture

### Ironing Implementation Location

The primary ironing implementation is located in:
- **File**: `src/libslic3r/Fill/Fill.cpp`
- **Function**: `Layer::make_ironing()` (lines 1618-1843)
- **Entry Point**: Called during layer processing after perimeters and infill are generated

### Ironing Configuration System

Ironing settings are defined in:
- **File**: `src/libslic3r/PrintConfig.hpp` (lines 1339-1353)
- **Key Settings**:
  - `ironing_type`: Enum (NoIroning, AllSolid, TopSurfaces, TopmostOnly)
  - `ironing_pattern`: InfillPattern for ironing lines
  - `ironing_flow`: Material flow percentage relative to layer height
  - `ironing_spacing`: Distance between ironing lines
  - `ironing_inset`: Inset from top surface edges
  - `ironing_speed`: Extrusion speed for ironing
  - `ironing_angle`: Angle of ironing lines
  - `ironing_angle_fixed`: Whether angle is fixed relative to model
  - Filament-specific overrides: `filament_ironing_flow`, `filament_ironing_spacing`, etc.

### Ironing Path Generation Process

1. **Region Classification**: Regions are classified by extruder and ironing parameters
2. **Surface Collection**: Top surfaces are collected based on `ironing_type` setting
3. **Area Calculation**: Ironing areas are calculated with inset from edges
4. **Fill Pattern Generation**: Ironing lines are generated using fill patterns (rectilinear, concentric, etc.)
5. **Extrusion Entity Creation**: Lines are converted to `ExtrusionEntityCollection` with `erIroning` role
6. **Flow Calculation**: Extrusion flow is calculated based on spacing, height, and flow percentage

### Extrusion Path Representation

**Key Classes**:
- `ExtrusionPath`: Represents a single extrusion path with `Polyline3` for 3D coordinates
- `ExtrusionEntityCollection`: Collection of extrusion entities
- `ExtrusionRole`: Enum defining path types, including `erIroning`

**Important Fields in ExtrusionPath**:
```cpp
Polyline3 polyline;              // 3D coordinates (x, y, z)
double mm3_per_mm;              // Volumetric flow rate
float width;                    // Extrusion width
float height;                   // Extrusion height
bool z_contoured;               // Flag for variable Z coordinates
```

**Polyline3 Structure**:
- Contains `Points3` which are 3D points with scaled coordinates
- Each point has x, y, z coordinates in internal units
- Z coordinates are stored as offsets from nominal layer height

### G-code Generation Pipeline

**Key Functions**:
1. `GCode::extrude_infill()` (line 7745): Processes ironing extrusions
2. `GCode::extrude_entity()` (line 7652): Dispatches to appropriate extrusion handler
3. `GCode::extrude_path()` (line 7669): Handles single path extrusion
4. `GCode::_extrude()` (line 7957): Core extrusion logic

**Variable Z Support**:
The G-code generator already supports variable Z through the `z_contoured` flag:
```cpp
if (path.z_contoured && !path.polyline.lines().empty()) {
    Vec2d dest2d = this->point_to_gcode(line.b.to_point());
    coordf_t z_diff = unscale_(line.b.z());
    double z = m_nominal_z + z_diff;
    gcode += m_writer.extrude_to_xyz(Vec3d(dest2d.x(), dest2d.y(), z), e, ...);
}
```

This shows that the infrastructure for variable-Z extrusion already exists in OrcaSlicer.

### Existing ZAA Implementation

OrcaSlicer already contains Z-axis anti-aliasing (ZAA) in:
- **File**: `src/libslic3r/ContourZ.cpp`
- **Function**: `Layer::make_contour_z()` (line 235)
- **Configuration**: `zaa_enabled`, `zaa_min_z`, `zaa_minimize_perimeter_height`

**ZAA Algorithm**:
1. Uses mesh raycasting to find surface height at each point
2. Calculates Z offset from nominal layer height
3. Constrains Z movement within configured limits
4. Applies to perimeters, top solid infill, and ironing
5. Modifies `ExtrusionPath::polyline` to include variable Z coordinates

**Key ZAA Constraints**:
- `zaa_min_z`: Maximum upward Z deviation
- Layer height: Maximum downward Z deviation
- Perimeter-specific: Additional slope-based adjustment for perimeters
- Ironing-specific: Allows larger Z range (full layer height + 0.1mm)

This implementation demonstrates that OrcaSlicer already has:
- Mesh querying infrastructure
- Variable Z path representation
- G-code emission for variable Z
- Safety constraints on Z movement

## Non-Planar Ironing Architecture

### Design Principles

1. **Preserve Existing Behavior**: When disabled, ironing must behave exactly as current OrcaSlicer
2. **Leverage Existing Infrastructure**: Reuse ZAA mesh querying and variable Z support where possible
3. **Independent Configuration**: Non-planar ironing should be separately configurable from ZAA
4. **Safety First**: All Z movement must be constrained within configurable limits
5. **Incremental Implementation**: Start with basic non-planar paths, add advanced features later

### Proposed Architecture

```
Normal Ironing (existing)
    |
    +--> Planar Ironing (existing behavior)
    |
    +--> Non-Planar Ironing (new)
             |
             +--> Surface sampling (mesh query or layer geometry)
             +--> Z interpolation (smooth transitions)
             +--> Constraint application (max deviation, max segment change)
             +--> Adaptive flow (optional, future work)
             +--> Variable-Z path generation
             +--> G-code emission (uses existing infrastructure)
```

### Integration Points

**1. Configuration System**
Add new settings to `PrintConfig.hpp`:
```cpp
// Non-planar ironing settings
((ConfigOptionBool, ironing_non_planar_enabled))
((ConfigOptionFloat, ironing_max_z_deviation))
((ConfigOptionFloat, ironing_max_z_change_per_segment))
((ConfigOptionFloat, ironing_sampling_resolution))
((ConfigOptionBool, ironing_adaptive_flow))  // Future work
```

**2. Ironing Path Generation**
Modify `Layer::make_ironing()` in `Fill.cpp`:
- After generating planar ironing paths
- Check if `ironing_non_planar_enabled` is true
- Apply non-planar Z modification to ironing paths
- Preserve existing planar behavior when disabled

**3. Surface Sampling**
Two potential approaches:
- **Mesh-based**: Similar to ZAA, use mesh raycasting to find surface height
- **Layer-based**: Use adjacent layer geometry to interpolate Z between layers

Mesh-based approach is preferred because:
- More accurate surface representation
- Already implemented in ZAA
- Works for complex geometries
- Aligns with existing OrcaSlicer infrastructure

**4. Z Interpolation Algorithm**
- For each point in ironing path, query surface height
- Calculate Z offset from nominal layer height
- Apply smoothing to avoid abrupt changes
- Constrain within configured limits
- Ensure continuous transitions between segments

**5. Safety Constraints**
Implement hard limits:
- `ironing_max_z_deviation`: Maximum absolute Z deviation from nominal
- `ironing_max_z_change_per_segment`: Maximum Z change between adjacent points
- Minimum Z: Prevent negative or unsafe Z values
- Boundary handling: Graceful degradation at edges/holes

**6. G-code Emission**
Use existing variable-Z infrastructure:
- Set `path.z_contoured = true` for non-planar ironing
- G-code generator already handles variable Z correctly
- No changes needed to G-code emission logic

**7. Preview System**
Investigate whether preview needs updates:
- Current preview may assume planar ironing
- Variable Z paths should be visualized correctly
- May need to ensure preview respects `z_contoured` flag

### Data Flow

```
User Configuration
    ↓
Layer::make_ironing()
    ↓
Generate planar ironing paths (existing)
    ↓
Check ironing_non_planar_enabled
    ↓
[if enabled]
    ↓
Surface sampling (mesh query)
    ↓
Z calculation and interpolation
    ↓
Apply safety constraints
    ↓
Set z_contoured flag
    ↓
[endif]
    ↓
ExtrusionEntityCollection with erIroning role
    ↓
GCode::extrude_infill()
    ↓
GCode::_extrude()
    ↓
Variable-Z G-code emission
```

### Adaptive Flow (Future Work)

**Concept**: Vary extrusion amount based on local geometry and Z deviation

**Potential Implementation**:
- Calculate local surface slope or curvature
- Adjust flow ratio based on geometric conditions
- Increase flow in seam/transition regions
- Decrease flow on flat surfaces

**Challenges**:
- Requires accurate geometric analysis
- May need experimental validation
- Risk of over/under extrusion
- Complex interaction with existing flow ratios

**Recommendation**: Implement basic non-planar paths first, investigate adaptive flow as separate phase

## Safety and Limitations

### Physical Constraints

**Critical Limitations**:
- Z deviation is not universally safe - depends on printer/toolhead geometry
- Typical clearance around nozzle is limited (approximately 0.3mm may be reasonable but not guaranteed)
- Toolhead body, fan shroud, heater block can collide with printed part
- Surface slope affects required clearance
- Layer height affects available Z envelope

**Software Constraints**:
- Maximum Z deviation: Configurable safety limit
- Maximum segment Z change: Prevent abrupt jumps
- Minimum Z: Prevent negative coordinates
- Boundary handling: Graceful degradation
- Invalid geometry: Fallback to planar behavior

### User Safety

**Required Documentation**:
- Clearly state that Z deviation values are experimental
- Recommend conservative values for initial testing
- Emphasize need for printer-specific validation
- Advise inspection of generated G-code before printing
- Document that software constraints ≠ physical collision safety

**Default Behavior**:
- Non-planar ironing disabled by default
- Conservative default Z deviation values
- Clear warnings in UI when enabled
- Recommended test procedure for first use

## Testing Strategy

### Unit Tests

1. **Planar Regression**: Verify existing ironing unchanged when feature disabled
2. **Flat Surface**: Verify approximately constant Z on flat geometry
3. **Sloped Surface**: Verify smoothly varying Z on slopes
4. **Curved Surface**: Verify smooth interpolation on curves
5. **Layer Boundary**: Verify continuous behavior through layer transitions
6. **Boundary Conditions**: Verify no abrupt jumps at edges/holes
7. **Z Constraints**: Verify max deviation and segment change limits respected
8. **Invalid Geometry**: Verify graceful handling of missing surface data
9. **G-code Verification**: Verify actual emitted G-code contains variable Z

### Integration Tests

1. **Existing Profiles**: Run representative OrcaSlicer profiles
2. **Build Verification**: Ensure project builds successfully
3. **G-code Inspection**: Manual inspection of generated test files
4. **Preview Verification**: Ensure preview displays correctly

### Physical Validation (Future)

1. **Test Models**: Create specific test geometries
2. **Conservative Values**: Start with small Z deviations
3. **Incremental Testing**: Gradually increase envelope
4. **Collision Inspection**: Visual inspection for toolhead collisions
5. **Surface Quality**: Measure surface improvement vs planar

## Implementation Phases

### Phase 1: Architecture Investigation (Current)
- Document existing ironing implementation
- Document ZAA implementation
- Identify integration points
- Design non-planar ironing architecture

### Phase 2: Basic Non-Planar Path Support
- Add configuration settings
- Implement basic surface sampling
- Implement Z interpolation
- Apply to ironing paths only
- Preserve planar behavior when disabled

### Phase 3: Safety Constraints
- Implement max Z deviation
- Implement max segment Z change
- Add boundary handling
- Add invalid geometry handling
- Add comprehensive tests

### Phase 4: G-code Verification
- Ensure variable Z survives to G-code
- Add G-code verification tests
- Test with various geometries
- Document G-code format

### Phase 5: Preview Support
- Investigate preview limitations
- Update preview if necessary
- Document any preview limitations

### Phase 6: Adaptive Flow (Optional)
- Investigate adaptive flow requirements
- Implement if feasible
- Or document as future work

### Phase 7: Integration and Testing
- Comprehensive testing
- Documentation updates
- Build verification
- Release preparation

## Licensing and Provenance

### BambuStudio-ZAA Reference

**License**: GNU Affero General Public License v3.0 (AGPL-3.0)
- Compatible with OrcaSlicer's AGPL-3.0 license
- Both projects share the same license family
- Code adaptation is legally permissible with proper attribution

**Repository Analysis**:
- BambuStudio-ZAA is a fork of BambuStudio (based on PrusaSlicer)
- Licensed under AGPL-3.0, same as OrcaSlicer
- Repository does not visibly expose ZAA-specific implementation files through web search
- Specific ZAA implementation files not directly accessible via web interface

**Implementation Approach**:
- **Primary Strategy**: Extend existing OrcaSlicer ZAA implementation (`ContourZ.cpp`)
- **Secondary Strategy**: Independent implementation using OrcaSlicer's mesh infrastructure
- **Rationale**: 
  - OrcaSlicer already has working ZAA implementation
  - Mesh querying infrastructure exists in OrcaSlicer
  - Avoids potential licensing complexity from direct code copying
  - Better integration with OrcaSlicer architecture
- **If Direct Adaptation Required**: Document provenance clearly in affected files

### OrcaSlicer License

**Current License**: AGPL-3.0
- Requires derivative works to be open source
- Requires source code distribution
- Must preserve license notices

**Implementation Approach**:
- Prefer independent implementation using OrcaSlicer architecture
- If adapting algorithms from ZAA, document provenance
- Ensure license compatibility
- Preserve all license notices

## Conclusion

This design document outlines a pragmatic approach to implementing non-planar ironing in OrcaSlicer by:

1. Leveraging existing ZAA infrastructure where possible
2. Preserving existing planar ironing behavior
3. Adding configurable safety constraints
4. Ensuring variable Z survives to G-code
5. Providing clear documentation of limitations

The implementation will be incremental, with each phase tested and committed separately. The feature will remain experimental until physically validated on actual hardware, with clear communication of software vs physical safety limitations.