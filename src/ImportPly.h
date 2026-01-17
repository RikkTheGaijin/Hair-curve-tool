#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ImportPly {
	struct ImportedLayer {
		int id = 0;
		std::string name;
		glm::vec3 color{0.90f, 0.75f, 0.22f};
		bool visible = true;
	};

	struct ImportedCurve {
		std::vector<glm::vec3> points;
		int anchorIndex = -1; // index of the vertex flagged as anchor/root, or -1
		int layerId = 0;
	};

	// Loads curves from an ASCII PLY file. Supports the HairTool export format:
	//   x y z anchor curve_id
	// If curve_id is missing, all vertices are treated as one curve.
	// If anchor is present and curve_id is missing, each anchor==1 starts a new curve.
	bool loadCurves(const std::string& path, std::vector<ImportedCurve>& outCurves, std::vector<ImportedLayer>* outLayers = nullptr, bool* outHasLayerInfo = nullptr, std::string* outError = nullptr);
}
