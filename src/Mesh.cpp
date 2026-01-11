#include "Mesh.h"

#include <glad/glad.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstdio>

struct Vertex {
	glm::vec3 p;
	glm::vec3 n;
	glm::vec2 uv;
};

static void updateBounds(glm::vec3& bmin, glm::vec3& bmax, const glm::vec3& p) {
	bmin = glm::min(bmin, p);
	bmax = glm::max(bmax, p);
}

bool Mesh::loadFromObj(const std::string& path) {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path,
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_GenNormals |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType |
		aiProcess_OptimizeMeshes |
		aiProcess_OptimizeGraph);

	if (!scene || !scene->mRootNode) {
		std::fprintf(stderr, "Assimp load failed: %s\n", importer.GetErrorString());
		return false;
	}

	m_positions.clear();
	m_normals.clear();
	m_uvs.clear();
	m_indices.clear();

	m_boundsMin = glm::vec3(1e30f);
	m_boundsMax = glm::vec3(-1e30f);

	// Scale factor: convert from centimeters (Maya default) to meters
	const float importScale = 0.01f;

	// Flatten all meshes
	unsigned int baseVertex = 0;
	for (unsigned int mi = 0; mi < scene->mNumMeshes; mi++) {
		const aiMesh* m = scene->mMeshes[mi];
		if (!m->HasPositions()) continue;

		for (unsigned int vi = 0; vi < m->mNumVertices; vi++) {
			aiVector3D ap = m->mVertices[vi];
			aiVector3D an = m->HasNormals() ? m->mNormals[vi] : aiVector3D(0, 1, 0);
			aiVector3D at = (m->HasTextureCoords(0) && m->mTextureCoords[0]) ? m->mTextureCoords[0][vi] : aiVector3D(0, 0, 0);
			glm::vec3 p(ap.x * importScale, ap.y * importScale, ap.z * importScale);
			glm::vec3 n(an.x, an.y, an.z);
			// Assimp/OBJ UV origin can differ from OpenGL texture coordinate convention.
			// Flip V to match common DCC exports (top-left) to OpenGL sampling (bottom-left).
			glm::vec2 uv(at.x, 1.0f - at.y);
			m_positions.push_back(p);
			m_normals.push_back(glm::normalize(n));
			m_uvs.push_back(uv);
			updateBounds(m_boundsMin, m_boundsMax, p);
		}

		for (unsigned int fi = 0; fi < m->mNumFaces; fi++) {
			const aiFace& f = m->mFaces[fi];
			if (f.mNumIndices != 3) continue;
			m_indices.push_back(baseVertex + f.mIndices[0]);
			m_indices.push_back(baseVertex + f.mIndices[1]);
			m_indices.push_back(baseVertex + f.mIndices[2]);
		}

		baseVertex += m->mNumVertices;
	}

	upload();
	return !m_positions.empty() && !m_indices.empty();
}

void Mesh::upload() {
	if (!m_vao) {
		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ebo);
	}

	std::vector<Vertex> verts;
	verts.reserve(m_positions.size());
	for (size_t i = 0; i < m_positions.size(); i++) {
		glm::vec2 uv = (i < m_uvs.size()) ? m_uvs[i] : glm::vec2(0.0f);
		verts.push_back(Vertex{m_positions[i], m_normals[i], uv});
	}

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int), m_indices.data(), GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, p));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, n));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));

	glBindVertexArray(0);
	m_indexCount = (int)m_indices.size();
}

void Mesh::draw() const {
	if (!m_vao) return;
	glBindVertexArray(m_vao);
	glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
}
