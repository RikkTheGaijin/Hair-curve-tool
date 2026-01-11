#pragma once

#include <glm/glm.hpp>

#include <vector>
#include <functional>

class Mesh;

class Bvh {
public:
	void build(const Mesh& mesh);

	// Calls callback(triIndex) for candidates hit by ray
	void raycast(const glm::vec3& ro, const glm::vec3& rd, const std::function<void(int)>& callback) const;

	// Finds closest point on any triangle (within maxDist if provided).
	// Returns false if BVH not built.
	bool nearestTriangle(const glm::vec3& p, int& outTriIndex, glm::vec3& outClosestPoint, glm::vec3& outNormal, float maxDist = 1e30f) const;

private:
	struct Node {
		glm::vec3 bmin{0}, bmax{0};
		int left = -1;
		int right = -1;
		int firstTri = 0;
		int triCount = 0;
	};

	std::vector<Node> m_nodes;
	std::vector<int> m_triIndices;
	const Mesh* m_mesh = nullptr;

	int buildNode(int first, int count);
	static bool rayAabb(const glm::vec3& ro, const glm::vec3& rdInv, const glm::vec3& bmin, const glm::vec3& bmax, float& tminOut, float& tmaxOut);
	static float aabbDistSq(const glm::vec3& p, const glm::vec3& bmin, const glm::vec3& bmax);
};
