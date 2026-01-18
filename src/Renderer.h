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
	float lastHairBuildMs() const { return m_hairBuildMs; }
	uint64_t hairRebuildCount() const { return m_hairRebuildCount; }
	bool hairRebuiltThisFrame() const { return m_hairRebuiltThisFrame; }

private:
	unsigned int m_meshProgram = 0;
	unsigned int m_lineProgram = 0;
	unsigned int m_hairProgram = 0;
	unsigned int m_meshTexture = 0;
	unsigned int m_gridVao = 0;
	unsigned int m_gridVbo = 0;
	int m_gridVertexCount = 0;
	unsigned int m_hairVao = 0;
	unsigned int m_hairTemplateVbo = 0;
	unsigned int m_hairInstanceVbo = 0;
	unsigned int m_hairPointBuffer = 0;
	unsigned int m_hairPointTex = 0;
	int m_hairTemplateVertexCount = 0;
	int m_hairInstanceCount = 0;
	int m_hairSteps = 0;
	uint64_t m_hairCacheKey = 0;
	bool m_hairCacheValid = false;
	float m_hairBuildMs = 0.0f;
	uint64_t m_hairRebuildCount = 0;
	bool m_hairRebuiltThisFrame = false;

	void createPrograms();
	unsigned int createProgram(const char* vs, const char* fs);

	void createGrid();
	void drawGrid(const Camera& camera, const glm::vec3& center, float scale) const;
	void uploadHair(const Scene& scene);
};
