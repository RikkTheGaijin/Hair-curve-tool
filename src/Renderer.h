#pragma once

#include <glm/glm.hpp>

class Scene;
class Camera;

class Renderer {
public:
	void init();
	void render(const Scene& scene, const Camera& camera);

private:
	unsigned int m_meshProgram = 0;
	unsigned int m_lineProgram = 0;
	unsigned int m_gridVao = 0;
	unsigned int m_gridVbo = 0;
	int m_gridVertexCount = 0;

	void createPrograms();
	unsigned int createProgram(const char* vs, const char* fs);

	void createGrid();
	void drawGrid(const Camera& camera, const glm::vec3& center, float scale) const;
};
