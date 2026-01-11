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
	unsigned int m_meshTexture = 0;
	unsigned int m_gridVao = 0;
	unsigned int m_gridVbo = 0;
	int m_gridVertexCount = 0;

	void createPrograms();
	unsigned int createProgram(const char* vs, const char* fs);

	void createGrid();
	void drawGrid(const Camera& camera, const glm::vec3& center, float scale) const;
};
