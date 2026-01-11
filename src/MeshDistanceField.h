#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

class Mesh;

// CPU-built nearest-surface field used by the CUDA solver for fast mesh collision.
// Stores per-voxel closest point (xyz) and an associated triangle normal (xyz).
class MeshDistanceField {
public:
	void clear();

	// Builds a uniform grid around mesh bounds expanded by padding.
	// resolution is the number of voxels per axis (cube).
	bool build(const Mesh& mesh, int resolution = 96, float padding = 0.03f);

	bool valid() const { return m_res > 0 && !m_cp.empty() && m_cp.size() == m_n.size(); }
	int resolution() const { return m_res; }
	float voxelSize() const { return m_voxel; }
	glm::vec3 origin() const { return m_origin; }

	const std::vector<glm::vec4>& closestPoints() const { return m_cp; }
	const std::vector<glm::vec4>& normals() const { return m_n; }

private:
	int m_res = 0;
	float m_voxel = 0.0f;
	glm::vec3 m_origin{0.0f};

	std::vector<glm::vec4> m_cp; // xyz=closest point, w unused
	std::vector<glm::vec4> m_n;  // xyz=triangle normal, w unused
};
