#pragma once

#include <string>

class Scene;

namespace ExportPly {
	bool exportCurvesAsPointCloud(const Scene& scene, const std::string& path);
}
