#include "ExportPly.h"

#include "Scene.h"
#include "HairGuides.h"

#include <fstream>
#include <vector>
#include <algorithm>

bool ExportPly::exportCurvesAsPointCloud(const Scene& scene, const std::string& path) {
	// Single-file export that preserves per-curve control point counts.
	// We emit per-vertex curve_id so importers can reconstruct variable-length strands.

	std::vector<size_t> exportCurves;
	std::vector<int> exportLayers;
	size_t vertexCount = 0;
	for (size_t ci = 0; ci < scene.guides().curveCount(); ci++) {
		const HairCurve& c = scene.guides().curve(ci);
		if (!c.visible) continue;
		if (c.points.size() >= 2) {
			exportCurves.push_back(ci);
			vertexCount += c.points.size();
			if (std::find(exportLayers.begin(), exportLayers.end(), c.layerId) == exportLayers.end()) {
				exportLayers.push_back(c.layerId);
			}
		}
	}
	if (vertexCount == 0) return false;

	std::ofstream f(path, std::ios::binary);
	if (!f.is_open()) return false;

	f << "ply\n";
	f << "format ascii 1.0\n";
	// Layer metadata for round-trip
	for (int layerId : exportLayers) {
		if (layerId < 0 || layerId >= (int)scene.layerCount()) continue;
		const LayerInfo& layer = scene.layer((size_t)layerId);
		f << "comment layer " << layerId << " \"" << layer.name << "\" "
		  << layer.color.r << " " << layer.color.g << " " << layer.color.b << " "
		  << (layer.visible ? 1 : 0) << "\n";
	}
	f << "element vertex " << vertexCount << "\n";
	f << "property float x\n";
	f << "property float y\n";
	f << "property float z\n";
	f << "property uchar anchor\n";
	f << "property int layer_id\n";
	f << "property int curve_id\n";
	f << "end_header\n";

	for (size_t ei = 0; ei < exportCurves.size(); ei++) {
		size_t ci = exportCurves[ei];
		const HairCurve& c = scene.guides().curve(ci);
		if (c.points.size() < 2) continue;
		for (size_t i = 0; i < c.points.size(); i++) {
			const glm::vec3& p = c.points[i];
			const int anchor = (i == 0 && c.root.triIndex >= 0) ? 1 : 0;
			f << p.x << " " << p.y << " " << p.z << " " << anchor << " " << c.layerId << " " << (int)ei << "\n";
		}
	}

	return true;
}
