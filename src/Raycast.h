#pragma once

#include <glm/glm.hpp>

class Mesh;

struct RayHit {
	bool hit = false;
	float t = 0.0f;
	int triIndex = -1;
	glm::vec3 bary{0.0f};
	glm::vec3 position{0.0f};
	glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

namespace Raycast {
	bool raycastMesh(const Mesh& mesh, const glm::vec3& ro, const glm::vec3& rd, RayHit& outHit);
	bool nearestOnMesh(const Mesh& mesh, const glm::vec3& p, RayHit& outHit, float maxDist = 1e30f);
}
