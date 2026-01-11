#pragma once

#include <glm/glm.hpp>

class Camera {
public:
	void setViewport(int width, int height);
	void reset();
	void frameBounds(const glm::vec3& bmin, const glm::vec3& bmax);

	glm::mat4 view() const;
	glm::mat4 proj() const;
	glm::mat4 viewProj() const;

	glm::vec3 position() const;
	glm::vec3 target() const { return m_target; }
	glm::vec3 forward() const;
	glm::vec3 right() const;
	glm::vec3 up() const;

	// Maya-style camera parameters
	float yaw() const { return m_yaw; }
	float pitch() const { return m_pitch; }
	float distance() const { return m_distance; }

	void orbit(float dx, float dy);
	void pan(float dx, float dy);
	void dolly(float dy);

	// Build a world ray from pixel coordinates in viewport
	void rayFromPixel(float px, float py, glm::vec3& outOrigin, glm::vec3& outDir) const;

	int viewportWidth() const { return m_width; }
	int viewportHeight() const { return m_height; }

private:
	int m_width = 1;
	int m_height = 1;

	glm::vec3 m_target{0.0f};
	float m_distance = 1.5f;
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;

	float m_fovY = glm::radians(45.0f);
	float m_near = 0.01f;
	float m_far = 1000.0f;
};
