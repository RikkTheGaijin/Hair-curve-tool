#include "Bvh.h"

#include "Mesh.h"

#include <algorithm>
#include <limits>

static glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
	// Real-Time Collision Detection (Christer Ericson) closest point on triangle
	glm::vec3 ab = b - a;
	glm::vec3 ac = c - a;
	glm::vec3 ap = p - a;
	float d1 = glm::dot(ab, ap);
	float d2 = glm::dot(ac, ap);
	if (d1 <= 0.0f && d2 <= 0.0f) return a;

	glm::vec3 bp = p - b;
	float d3 = glm::dot(ab, bp);
	float d4 = glm::dot(ac, bp);
	if (d3 >= 0.0f && d4 <= d3) return b;

	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		float v = d1 / (d1 - d3);
		return a + ab * v;
	}

	glm::vec3 cp = p - c;
	float d5 = glm::dot(ab, cp);
	float d6 = glm::dot(ac, cp);
	if (d6 >= 0.0f && d5 <= d6) return c;

	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		float w = d2 / (d2 - d6);
		return a + ac * w;
	}

	float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return b + (c - b) * w;
	}

	float denom = 1.0f / (va + vb + vc);
	float v = vb * denom;
	float w = vc * denom;
	return a + ab * v + ac * w;
}

static void triBounds(const std::vector<glm::vec3>& pos, const std::vector<unsigned int>& ind, int triIndex, glm::vec3& outMin, glm::vec3& outMax) {
	unsigned int i0 = ind[(size_t)triIndex * 3 + 0];
	unsigned int i1 = ind[(size_t)triIndex * 3 + 1];
	unsigned int i2 = ind[(size_t)triIndex * 3 + 2];
	outMin = glm::min(pos[i0], glm::min(pos[i1], pos[i2]));
	outMax = glm::max(pos[i0], glm::max(pos[i1], pos[i2]));
}

void Bvh::build(const Mesh& mesh) {
	m_mesh = &mesh;
	m_nodes.clear();
	m_triIndices.clear();

	int triCount = (int)mesh.indices().size() / 3;
	m_triIndices.resize(triCount);
	for (int i = 0; i < triCount; i++) m_triIndices[i] = i;

	m_nodes.reserve(triCount * 2);
	buildNode(0, triCount);
}

int Bvh::buildNode(int first, int count) {
	Node node;
	node.firstTri = first;
	node.triCount = count;

	const auto& pos = m_mesh->positions();
	const auto& ind = m_mesh->indices();

	glm::vec3 bmin( std::numeric_limits<float>::infinity());
	glm::vec3 bmax(-std::numeric_limits<float>::infinity());

	for (int i = 0; i < count; i++) {
		int tri = m_triIndices[(size_t)first + i];
		glm::vec3 tmin, tmax;
		triBounds(pos, ind, tri, tmin, tmax);
		bmin = glm::min(bmin, tmin);
		bmax = glm::max(bmax, tmax);
	}

	node.bmin = bmin;
	node.bmax = bmax;

	int nodeIndex = (int)m_nodes.size();
	m_nodes.push_back(node);

	if (count <= 8) {
		return nodeIndex;
	}

	glm::vec3 extent = bmax - bmin;
	int axis = 0;
	if (extent.y > extent.x) axis = 1;
	if (extent.z > extent[axis]) axis = 2;

	auto triCenter = [&](int tri) {
		glm::vec3 tmin, tmax;
		triBounds(pos, ind, tri, tmin, tmax);
		return 0.5f * (tmin + tmax);
	};

	int mid = first + count / 2;
	std::nth_element(m_triIndices.begin() + first, m_triIndices.begin() + mid, m_triIndices.begin() + first + count,
		[&](int a, int b) { return triCenter(a)[axis] < triCenter(b)[axis]; });

	int left = buildNode(first, count / 2);
	int right = buildNode(mid, count - count / 2);

	m_nodes[nodeIndex].left = left;
	m_nodes[nodeIndex].right = right;
	m_nodes[nodeIndex].triCount = 0;
	return nodeIndex;
}

bool Bvh::rayAabb(const glm::vec3& ro, const glm::vec3& rdInv, const glm::vec3& bmin, const glm::vec3& bmax, float& tminOut, float& tmaxOut) {
	float tx1 = (bmin.x - ro.x) * rdInv.x;
	float tx2 = (bmax.x - ro.x) * rdInv.x;
	float tmin = glm::min(tx1, tx2);
	float tmax = glm::max(tx1, tx2);

	float ty1 = (bmin.y - ro.y) * rdInv.y;
	float ty2 = (bmax.y - ro.y) * rdInv.y;
	tmin = glm::max(tmin, glm::min(ty1, ty2));
	tmax = glm::min(tmax, glm::max(ty1, ty2));

	float tz1 = (bmin.z - ro.z) * rdInv.z;
	float tz2 = (bmax.z - ro.z) * rdInv.z;
	tmin = glm::max(tmin, glm::min(tz1, tz2));
	tmax = glm::min(tmax, glm::max(tz1, tz2));

	tminOut = tmin;
	tmaxOut = tmax;
	return tmax >= tmin && tmax >= 0.0f;
}

float Bvh::aabbDistSq(const glm::vec3& p, const glm::vec3& bmin, const glm::vec3& bmax) {
	float dx = 0.0f;
	if (p.x < bmin.x) dx = bmin.x - p.x;
	else if (p.x > bmax.x) dx = p.x - bmax.x;
	float dy = 0.0f;
	if (p.y < bmin.y) dy = bmin.y - p.y;
	else if (p.y > bmax.y) dy = p.y - bmax.y;
	float dz = 0.0f;
	if (p.z < bmin.z) dz = bmin.z - p.z;
	else if (p.z > bmax.z) dz = p.z - bmax.z;
	return dx * dx + dy * dy + dz * dz;
}

void Bvh::raycast(const glm::vec3& ro, const glm::vec3& rd, const std::function<void(int)>& callback) const {
	if (m_nodes.empty()) return;
	glm::vec3 rdInv(1.0f / rd.x, 1.0f / rd.y, 1.0f / rd.z);

	std::vector<int> stack;
	stack.reserve(128);
	stack.push_back(0);

	while (!stack.empty()) {
		int ni = stack.back();
		stack.pop_back();
		const Node& n = m_nodes[(size_t)ni];
		float tmin = 0, tmax = 0;
		if (!rayAabb(ro, rdInv, n.bmin, n.bmax, tmin, tmax)) continue;

		if (n.triCount > 0) {
			for (int i = 0; i < n.triCount; i++) {
				callback(m_triIndices[(size_t)n.firstTri + i]);
			}
			continue;
		}

		if (n.left >= 0) stack.push_back(n.left);
		if (n.right >= 0) stack.push_back(n.right);
	}
}

bool Bvh::nearestTriangle(const glm::vec3& p, int& outTriIndex, glm::vec3& outClosestPoint, glm::vec3& outNormal, float maxDist) const {
	if (m_nodes.empty() || !m_mesh) return false;

	const auto& pos = m_mesh->positions();
	const auto& ind = m_mesh->indices();
	if (pos.empty() || ind.empty()) return false;

	float bestDistSq = maxDist * maxDist;
	int bestTri = -1;
	glm::vec3 bestP(0.0f);
	glm::vec3 bestN(0.0f, 1.0f, 0.0f);

	std::vector<int> stack;
	stack.reserve(128);
	stack.push_back(0);

	while (!stack.empty()) {
		int ni = stack.back();
		stack.pop_back();
		const Node& n = m_nodes[(size_t)ni];
		float d2 = aabbDistSq(p, n.bmin, n.bmax);
		if (d2 > bestDistSq) continue;

		if (n.triCount > 0) {
			for (int i = 0; i < n.triCount; i++) {
				int tri = m_triIndices[(size_t)n.firstTri + i];
				unsigned int i0 = ind[(size_t)tri * 3 + 0];
				unsigned int i1 = ind[(size_t)tri * 3 + 1];
				unsigned int i2 = ind[(size_t)tri * 3 + 2];
				glm::vec3 a = pos[i0];
				glm::vec3 b = pos[i1];
				glm::vec3 c = pos[i2];
				glm::vec3 cp = closestPointOnTriangle(p, a, b, c);
				glm::vec3 d = p - cp;
				float dd = glm::dot(d, d);
				if (dd < bestDistSq) {
					bestDistSq = dd;
					bestTri = tri;
					bestP = cp;
					bestN = glm::normalize(glm::cross(b - a, c - a));
				}
			}
			continue;
		}

		if (n.left >= 0) stack.push_back(n.left);
		if (n.right >= 0) stack.push_back(n.right);
	}

	if (bestTri < 0) return false;
	outTriIndex = bestTri;
	outClosestPoint = bestP;
	outNormal = bestN;
	return true;
}
