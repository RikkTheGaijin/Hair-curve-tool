#pragma once

#include <string>

class Scene;
class Camera;

namespace Serialization {
	bool saveScene(const Scene& scene, const Camera& camera, const std::string& path);
	bool loadScene(Scene& scene, Camera* camera, const std::string& path, bool* outCameraRestored = nullptr);
}
