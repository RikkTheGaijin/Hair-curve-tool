#pragma once

#include <string>

class Scene;

namespace Serialization {
	bool saveScene(const Scene& scene, const std::string& path);
	bool loadScene(Scene& scene, const std::string& path);
}
