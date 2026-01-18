#pragma once

#include <glm/glm.hpp>

#include <string>

class Scene;
class Camera;

class Renderer {
public:
	void init();
	void render(const Scene& scene, const Camera& camera);
	bool loadMeshTexture(const std::string& path);
	void clearMeshTexture();

private:
	unsigned int m_meshProgram = 0;
	unsigned int m_lineProgram = 0;
	unsigned int m_hairProgram = 0;
	unsigned int m_meshTexture = 0;
	unsigned int m_gridVao = 0;
	unsigned int m_gridVbo = 0;
	int m_gridVertexCount = 0;
	unsigned int m_hairVao = 0;
	unsigned int m_hairVbo = 0;
	unsigned int m_hairEbo = 0;
	int m_hairIndexCount = 0;
	uint64_t m_hairCacheKey = 0;
	bool m_hairCacheValid = false;

	void createPrograms();
	unsigned int createProgram(const char* vs, const char* fs);

	void createGrid();
	void drawGrid(const Camera& camera, const glm::vec3& center, float scale) const;
	void uploadHair(const Scene& scene);
};
