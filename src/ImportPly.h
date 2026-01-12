#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ImportPly {
	struct ImportedCurve {
		std::vector<glm::vec3> points;
		int anchorIndex = -1; // index of the vertex flagged as anchor/root, or -1
	};

	// Loads curves from an ASCII PLY file. Supports the HairTool export format:
	//   x y z anchor curve_id
	// If curve_id is missing, all vertices are treated as one curve.
	// If anchor is present and curve_id is missing, each anchor==1 starts a new curve.
	bool loadCurves(const std::string& path, std::vector<ImportedCurve>& outCurves, std::string* outError = nullptr);
}
