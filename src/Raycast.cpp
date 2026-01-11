#include "Raycast.h"

#include "Mesh.h"
#include "Bvh.h"

#include <limits>
#include <cmath>

static bool rayTri(const glm::vec3& ro, const glm::vec3& rd,
	const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
	float& t, glm::vec3& bary) {
	// Moller-Trumbore
	glm::vec3 e1 = b - a;
	glm::vec3 e2 = c - a;
	glm::vec3 p = glm::cross(rd, e2);
	float det = glm::dot(e1, p);
	if (glm::abs(det) < 1e-8f) return false;
	float invDet = 1.0f / det;
	glm::vec3 s = ro - a;
	float u = glm::dot(s, p) * invDet;
	if (u < 0.0f || u > 1.0f) return false;
	glm::vec3 q = glm::cross(s, e1);
	float v = glm::dot(rd, q) * invDet;
	if (v < 0.0f || u + v > 1.0f) return false;
	float tt = glm::dot(e2, q) * invDet;
	if (tt < 0.0f) return false;

	t = tt;
	bary = glm::vec3(1.0f - u - v, u, v);
	return true;
}

bool Raycast::raycastMesh(const Mesh& mesh, const glm::vec3& ro, const glm::vec3& rd, RayHit& outHit) {
	outHit = {};
	const std::vector<glm::vec3>& pos = mesh.positions();
	const std::vector<unsigned int>& ind = mesh.indices();
	if (pos.empty() || ind.empty()) return false;

	// BVH built lazily per mesh
	static const Mesh* cachedMesh = nullptr;
	static Bvh bvh;
	if (cachedMesh != &mesh) {
		bvh.build(mesh);
		cachedMesh = &mesh;
	}

	float bestT = std::numeric_limits<float>::infinity();
	int bestTri = -1;
	glm::vec3 bestBary(0.0f);

	bvh.raycast(ro, rd, [&](int triIndex) {
		unsigned int i0 = ind[(size_t)triIndex * 3 + 0];
		unsigned int i1 = ind[(size_t)triIndex * 3 + 1];
		unsigned int i2 = ind[(size_t)triIndex * 3 + 2];
		float t = 0.0f;
		glm::vec3 bary;
		if (!rayTri(ro, rd, pos[i0], pos[i1], pos[i2], t, bary)) return;
		if (t < bestT) {
			bestT = t;
			bestTri = triIndex;
			bestBary = bary;
		}
	});

	if (bestTri < 0 || !std::isfinite(bestT)) return false;

	unsigned int i0 = ind[(size_t)bestTri * 3 + 0];
	unsigned int i1 = ind[(size_t)bestTri * 3 + 1];
	unsigned int i2 = ind[(size_t)bestTri * 3 + 2];
	glm::vec3 p = pos[i0] * bestBary.x + pos[i1] * bestBary.y + pos[i2] * bestBary.z;
	glm::vec3 n = glm::normalize(glm::cross(pos[i1] - pos[i0], pos[i2] - pos[i0]));
	if (!mesh.normals().empty()) {
		const auto& nrm = mesh.normals();
		n = glm::normalize(nrm[i0] * bestBary.x + nrm[i1] * bestBary.y + nrm[i2] * bestBary.z);
	}

	outHit.hit = true;
	outHit.t = bestT;
	outHit.triIndex = bestTri;
	outHit.bary = bestBary;
	outHit.position = p;
	outHit.normal = n;
	return true;
}
