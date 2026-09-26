#include "Exception.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "SLA/IndexedMesh.hpp"
#include "libslic3r.h"
#include <cfloat>
#include <cmath>
#include <initializer_list>
#include <string>

namespace Slic3r {

static void non_planar_ironing_extrusion_entity(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntity *extr);

static bool non_planar_ironing_extrusion_path(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionPath &path)
{
    if (path.role() != erIroning) {
        return false;
    }

    Layer *layer = region->layer();
    const PrintRegionConfig &config = region->region().config();

    if (!config.ironing_non_planar_enabled) {
        return false;
    }

    coordf_t mesh_slice_z = layer->slice_z + mesh.ground_level();
    coordf_t max_z_deviation = config.ironing_max_z_deviation;
    coordf_t max_z_change = config.ironing_max_z_change_per_segment;

    const Points3 &points = path.polyline.points;
    double resolution_mm = 0.1;  // Same resolution as ZAA for consistency

    coordf_t height = layer->height;

    Pointf3s non_planar_points;
    bool was_modified = false;

    if (points.size() < 2) {
        // Safety check. The loop below does not handle paths with less than two points correctly.
        return false;
    }

    for (Points3::const_iterator it = points.begin(); it != points.end() - 1; ++it) {
        Vec2d p1d(unscale_(it->x()), unscale_(it->y()));
        Vec2d p2d(unscale_((it + 1)->x()), unscale_((it + 1)->y()));
        Linef line(p1d, p2d);

        double length_mm = line.length();
        int num_segments = int(std::ceil(length_mm / resolution_mm));
        Vec2d delta = line.vector();

        if (num_segments == 0) {
            continue;
        }

        for (int i = 0; i < num_segments + 1; i++) {
            Vec2d p = p1d + delta * i / num_segments;

            coordf_t x = p.x();
            coordf_t y = p.y();

            // Query mesh to find surface height at this point
            sla::IndexedMesh::hit_result hit_up = mesh.query_ray_hit({x, y, mesh_slice_z}, {0.0, 0.0, 1.0});

            double z_offset = 0.0;

            if (hit_up.is_hit()) {
                // Calculate Z offset from nominal layer height
                z_offset = hit_up.distance() - (layer->print_z - layer->slice_z);

                // Constrain within maximum deviation
                if (z_offset > max_z_deviation) {
                    z_offset = max_z_deviation;
                } else if (z_offset < -max_z_deviation) {
                    z_offset = -max_z_deviation;
                }

                // Reject points that are too far from mesh (likely not top surface)
                if (std::abs(z_offset) > height + 0.1) {
                    z_offset = 0.0;
                }
            } else {
                // No mesh hit - use planar behavior
                z_offset = 0.0;
            }

            // Apply per-segment Z change constraint
            if (!non_planar_points.empty()) {
                double last_z = non_planar_points.back().z();
                double z_change = z_offset - last_z;
                if (std::abs(z_change) > max_z_change) {
                    // Clamp the change to maximum allowed
                    z_offset = last_z + (z_change > 0 ? max_z_change : -max_z_change);
                }
            }

            if (std::abs(z_offset) > EPSILON) {
                was_modified = true;
            }

            Vec3d new_point = {p.x(), p.y(), z_offset};

            // Collinear point optimization (same as ZAA)
            if (non_planar_points.size() >= 2 && i != 0) {
                double dist = Linef3::distance_to_infinite_squared(new_point, 
                                                                   non_planar_points[non_planar_points.size() - 2],
                                                                   non_planar_points[non_planar_points.size() - 1]);
                if (dist < EPSILON * EPSILON) {
                    non_planar_points[non_planar_points.size() - 1] = new_point;
                    continue;
                }
            }

            non_planar_points.push_back(new_point);
        }
    }

    if (!was_modified) {
        return false;
    }

    // Convert back to scaled coordinates
    Polyline3 polyline;
    for (const Vec3d &point : non_planar_points) {
        polyline.append(Point3(scale_(point.x()), scale_(point.y()), scale_(point.z())));
    }

    path.polyline = std::move(polyline);
    path.z_contoured = true;
    return true;
}

static void non_planar_ironing_extrusion_multipath(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionMultiPath &multipath)
{
    for (ExtrusionPath &path : multipath.paths) {
        non_planar_ironing_extrusion_path(region, mesh, path);
    }
}

static void non_planar_ironing_extrusion_loop(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionLoop &loop)
{
    for (ExtrusionPath &path : loop.paths) {
        non_planar_ironing_extrusion_path(region, mesh, path);
    }
}

static void non_planar_ironing_extrusion_entity_collection(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntityCollection &collection)
{
    for (ExtrusionEntity *entity : collection.entities) {
        non_planar_ironing_extrusion_entity(region, mesh, entity);
    }
}

static void non_planar_ironing_extrusion_entity(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntity *extr)
{
    ExtrusionMultiPath *multipath = dynamic_cast<ExtrusionMultiPath*>(extr);
    if (multipath != nullptr) {
        non_planar_ironing_extrusion_multipath(region, mesh, *multipath);
        return;
    }

    ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(extr);
    if (path != nullptr) {
        non_planar_ironing_extrusion_path(region, mesh, *path);
        return;
    }

    ExtrusionLoop *loop = dynamic_cast<ExtrusionLoop*>(extr);
    if (loop != nullptr) {
        non_planar_ironing_extrusion_loop(region, mesh, *loop);
        return;
    }

    ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection*>(extr);
    if (collection != nullptr) {
        non_planar_ironing_extrusion_entity_collection(region, mesh, *collection);
        return;
    }

    // Other types (sloped paths, etc.) are not handled
    return;
}

static void handle_non_planar_ironing_collection(LayerRegion *region, const sla::IndexedMesh &mesh, ExtrusionEntityCollection &collection) {
    for (ExtrusionEntity* extr : collection.entities) {
        if (extr->role() != erIroning) {
            continue;
        }

        non_planar_ironing_extrusion_entity(region, mesh, extr);
    }
}

void Layer::make_non_planar_ironing(const sla::IndexedMesh &mesh)
{
    for (LayerRegion *region : this->regions()) {
        if (!region->region().config().ironing_non_planar_enabled) {
            continue;
        }

        handle_non_planar_ironing_collection(region, mesh, region->fills);
    }
}

} // namespace Slic3r