#include "MeshDistanceField.h"

#include "Mesh.h"
#include "Bvh.h"

#include <algorithm>

void MeshDistanceField::clear() {
	m_res = 0;
	m_voxel = 0.0f;
	m_origin = glm::vec3(0.0f);
	m_cp.clear();
	m_n.clear();
}

bool MeshDistanceField::build(const Mesh& mesh, int resolution, float padding) {
	clear();

	resolution = std::clamp(resolution, 16, 256);
	padding = glm::max(padding, 0.0f);

	glm::vec3 bmin = mesh.boundsMin() - glm::vec3(padding);
	glm::vec3 bmax = mesh.boundsMax() + glm::vec3(padding);
	glm::vec3 extent = bmax - bmin;
	float maxAxis = glm::max(extent.x, glm::max(extent.y, extent.z));
	if (maxAxis < 1e-6f) return false;

	m_res = resolution;
	m_voxel = maxAxis / (float)(m_res - 1);
	m_origin = bmin;

	size_t count = (size_t)m_res * (size_t)m_res * (size_t)m_res;
	m_cp.resize(count);
	m_n.resize(count);

	Bvh bvh;
	bvh.build(mesh);

	for (int z = 0; z < m_res; z++) {
		for (int y = 0; y < m_res; y++) {
			for (int x = 0; x < m_res; x++) {
				glm::vec3 p = m_origin + glm::vec3((float)x, (float)y, (float)z) * m_voxel;

				int tri = -1;
				glm::vec3 cp, n;
				bool ok = bvh.nearestTriangle(p, tri, cp, n);
				size_t idx = (size_t)x + (size_t)m_res * ((size_t)y + (size_t)m_res * (size_t)z);
				if (ok) {
					m_cp[idx] = glm::vec4(cp, 0.0f);
					m_n[idx] = glm::vec4(n, 0.0f);
				} else {
					m_cp[idx] = glm::vec4(p, 0.0f);
					m_n[idx] = glm::vec4(0, 1, 0, 0);
				}
			}
		}
	}
	return true;
}
