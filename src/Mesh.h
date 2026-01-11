#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

class Mesh {
public:
	bool loadFromObj(const std::string& path);
	void draw() const;

	const std::vector<glm::vec3>& positions() const { return m_positions; }
	const std::vector<glm::vec3>& normals() const { return m_normals; }
	const std::vector<glm::vec2>& uvs() const { return m_uvs; }
	const std::vector<unsigned int>& indices() const { return m_indices; }

	glm::vec3 boundsMin() const { return m_boundsMin; }
	glm::vec3 boundsMax() const { return m_boundsMax; }

private:
	std::vector<glm::vec3> m_positions;
	std::vector<glm::vec3> m_normals;
	std::vector<glm::vec2> m_uvs;
	std::vector<unsigned int> m_indices;

	glm::vec3 m_boundsMin{0};
	glm::vec3 m_boundsMax{0};

	unsigned int m_vao = 0;
	unsigned int m_vbo = 0;
	unsigned int m_ebo = 0;
	int m_indexCount = 0;

	void upload();
};
