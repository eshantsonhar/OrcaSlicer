# Non-Planar Ironing for OrcaSlicer

This project investigates a new approach to achieving smoother FDM printed surfaces by combining non-planar ironing with conventional layer-based printing.

## Project Goal

Traditional FDM printing constructs objects as stacked planar layers, which creates visible layer stair-stepping and surface seams. This project explores whether controlled non-planar ironing can reduce these artifacts while remaining within the mechanical constraints of conventional FDM printers.

The research focuses on exploiting the small region around the nozzle that is available for controlled Z movement without causing the rest of the toolhead to collide with the printed object.

## Motivation

The project is motivated by several observations:

- Conventional FDM is technically three-dimensional, but most extrusion occurs on planar layer surfaces
- Layer stair-stepping remains visible even with optimal layer heights
- True arbitrary non-planar printing requires specialized hardware (robotic arms, multi-axis systems)
- Z-axis anti-aliasing (ZAA) has demonstrated significant reduction in stair-stepping
- The question remains: can non-planar ironing further smooth surfaces beyond what ZAA achieves?

The target hardware is conventional FDM printers such as the Bambu Lab A1, not specialized multi-axis systems.

## Inspiration

This project was inspired by:

- The observation that conventional FDM mostly constructs objects as stacked planar layers
- The mechanical difficulty of true arbitrary non-planar printing on standard hardware
- The discovery of Z-axis anti-aliasing in BambuStudio-ZAA
- The significant reduction in stair-stepping achievable with ZAA
- The question of whether non-planar ironing could further smooth the surface

### Reference Implementation

This project references the Z-axis anti-aliasing work in [BambuStudio-ZAA](https://github.com/adob/BambuStudio-ZAA) by adob. The ZAA implementation demonstrates that variable-Z toolpaths can be generated and executed on conventional FDM hardware.

## Core Concept

The proposed concept is to allow the ironing toolpath to move through a small controlled Z envelope around layer transitions, rather than forcing the ironing pass to remain on one perfectly flat plane.

### Inter-Layer Ironing

Instead of treating each layer boundary as an abrupt surface:

```
Layer N       Z = 0.20
-----------------------
transition
-----------------------
Layer N+1     Z = 0.40
```

The experimental ironing path may use a controlled Z profile through part of that transition region.

### Z Envelope

For typical 0.2 mm layer heights, the experimental concept investigates whether a controlled ironing envelope on the order of approximately 0.3 mm can be used while remaining within the physical clearance available around the nozzle.

**Important**: These values are not universally safe. The actual allowable Z envelope depends on:
- Nozzle geometry and protrusion
- Heater block geometry
- Fan shroud design
- Toolhead body dimensions
- Printed geometry and local surface slope
- Printer kinematics
- Printer-specific collision envelope

Software constraints cannot guarantee physical collision safety without printer-specific geometric analysis or physical testing.

## Implementation Status

### Completed
- Architecture analysis of OrcaSlicer ironing system
- Analysis of existing ZAA implementation in OrcaSlicer
- BambuStudio-ZAA license compatibility analysis
- Technical design documentation

### In Progress
- Non-planar ironing path representation
- Surface-aware Z interpolation
- Safety constraint implementation

### Planned
- User-facing configuration
- G-code verification
- Preview system updates
- Adaptive flow investigation
- Automated testing
- Physical validation preparation

## Technical Approach

### Architecture

The implementation leverages OrcaSlicer's existing infrastructure:

- **Variable-Z Support**: OrcaSlicer already supports variable-Z extrusion through the `z_contoured` flag
- **Mesh Querying**: Existing ZAA implementation uses mesh raycasting for surface height
- **G-code Emission**: The G-code generator correctly handles variable-Z coordinates
- **Configuration System**: Well-established settings architecture for new parameters

### Integration Point

Non-planar ironing integrates into the existing ironing pipeline:

```
Normal Ironing
    |
    +--> Planar Ironing (existing behavior)
    |
    +--> Non-Planar Ironing (new)
             |
             +--> Surface sampling (mesh query)
             +--> Z interpolation (smooth transitions)
             +--> Constraint application (safety limits)
             +--> Variable-Z path generation
             +--> G-code emission (existing infrastructure)
```

### Safety Constraints

The implementation includes software-level constraints:

- **Maximum Z Deviation**: Configurable limit on absolute Z deviation from nominal layer height
- **Maximum Segment Z Change**: Prevents abrupt Z jumps between adjacent path segments
- **Minimum Z**: Prevents negative or unsafe Z coordinates
- **Boundary Handling**: Graceful degradation at edges, holes, and invalid geometry
- **Invalid Geometry**: Fallback to planar behavior when surface information is unavailable

## Limitations

### Physical Constraints

- Z deviation is not universally safe - depends on printer/toolhead geometry
- Typical clearance around nozzle is limited
- Toolhead body, fan shroud, heater block can collide with printed part
- Surface slope affects required clearance
- Layer height affects available Z envelope

### Software Constraints

- Cannot guarantee physical collision safety through software alone
- Requires printer-specific validation for safe Z deviation values
- Mesh-based surface sampling assumes accurate mesh representation
- Complex geometries may have unreliable surface information

### Validation Status

**Software Verified**: The implementation can be verified to:
- Generate variable-Z ironing paths
- Respect configured Z constraints
- Emit correct G-code with variable Z
- Handle edge cases gracefully

**Physically Unvalidated**: Physical safety requires:
- Printer-specific geometric analysis
- Experimental testing with conservative Z values
- Visual inspection for toolhead collisions
- Measurement of actual surface quality improvement

## Development

This project is based on [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer), a fork of PrusaSlicer with additional features and improvements.

### Reference Projects

- **Upstream**: [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
- **ZAA Reference**: [BambuStudio-ZAA](https://github.com/adob/BambuStudio-ZAA) by adob

### Repository Structure

```
OrcaSlicer/
├── README.md                      # This file
├── docs/
│   └── non_planar_ironing_design.md  # Technical design document
├── src/
│   ├── libslic3r/
│   │   ├── Fill/Fill.cpp          # Ironing path generation
│   │   ├── ContourZ.cpp           # Existing ZAA implementation
│   │   ├── GCode.cpp              # G-code generation
│   │   └── PrintConfig.hpp        # Configuration definitions
│   └── slic3r/GUI/                # User interface
└── resources/profiles/            # Printer profiles
```

## Building

See the OrcaSlicer build documentation for platform-specific build instructions. This project follows the same build process as upstream OrcaSlicer.

## Usage

When implemented, non-planar ironing will be configurable through OrcaSlicer's standard settings interface. The feature will be disabled by default to preserve existing behavior.

### Configuration Settings (Planned)

- **Non-Planar Ironing Enable**: Master switch for the feature
- **Maximum Z Deviation**: Safety limit for Z deviation from nominal layer height
- **Maximum Z Change per Segment**: Limit on Z change between adjacent path points
- **Sampling Resolution**: Resolution for surface height sampling

### Recommended Initial Values

For initial testing with conservative safety margins:
- Maximum Z Deviation: 0.1-0.2 mm
- Maximum Z Change per Segment: 0.05 mm
- Start with simple geometries (flat surfaces, gentle slopes)

## Safety Warnings

**Important Safety Information**:

1. **Experimental Feature**: This is experimental software. Generated G-code should be inspected before attempting prints.

2. **Collision Risk**: Software constraints cannot guarantee physical collision safety. The actual safe Z envelope depends on your specific printer and toolhead geometry.

3. **Conservative Testing**: Start with very conservative Z deviation values and small test models. Gradually increase after validating safety.

4. **Visual Inspection**: Monitor first prints carefully for toolhead collisions, especially near complex geometries.

5. **Printer-Specific**: Safe values for one printer may not be safe for another due to different toolhead designs.

6. **No Guarantee**: The authors provide no guarantee of physical safety or print quality.

## Contributing

This is a research project. Contributions should focus on:
- Improving the safety and robustness of non-planar path generation
- Adding automated tests for edge cases
- Improving documentation
- Physical validation results and findings

## License

This project is based on OrcaSlicer, which is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). The non-planar ironing implementation follows the same license.

### Reference Project Licenses

- **OrcaSlicer**: AGPL-3.0
- **BambuStudio-ZAA**: AGPL-3.0 (compatible)

## Acknowledgments

- OrcaSlicer development team for the excellent slicer foundation
- adob for the BambuStudio-ZAA reference implementation
- The broader FDM printing community for continued innovation