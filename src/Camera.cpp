#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

void Camera::setViewport(int width, int height) {
	m_width = width < 1 ? 1 : width;
	m_height = height < 1 ? 1 : height;
}

void Camera::reset() {
	m_target = glm::vec3(0.0f, 0.0f, 0.0f);
	m_distance = 1.5f;
	m_yaw = glm::radians(45.0f);
	m_pitch = glm::radians(-15.0f);
}

void Camera::frameBounds(const glm::vec3& bmin, const glm::vec3& bmax) {
	glm::vec3 c = 0.5f * (bmin + bmax);
	glm::vec3 e = 0.5f * (bmax - bmin);
	float r = glm::length(e);
	if (r < 1e-4f) r = 1.0f;

	m_target = c;
	m_distance = r / glm::tan(m_fovY * 0.5f) * 1.2f;
	m_near = glm::max(0.001f, m_distance * 0.001f);
	m_far = m_distance + r * 10.0f;
}

glm::vec3 Camera::forward() const {
	glm::vec3 f;
	f.x = glm::cos(m_pitch) * glm::sin(m_yaw);
	f.y = glm::sin(m_pitch);
	f.z = glm::cos(m_pitch) * glm::cos(m_yaw);
	return glm::normalize(f);
}

glm::vec3 Camera::right() const {
	return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0)));
}

glm::vec3 Camera::up() const {
	return glm::normalize(glm::cross(right(), forward()));
}

glm::vec3 Camera::position() const {
	return m_target - forward() * m_distance;
}

glm::mat4 Camera::view() const {
	return glm::lookAt(position(), m_target, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::proj() const {
	float aspect = (float)m_width / (float)m_height;
	return glm::perspective(m_fovY, aspect, m_near, m_far);
}

glm::mat4 Camera::viewProj() const {
	return proj() * view();
}

void Camera::orbit(float dx, float dy) {
	m_yaw += dx;
	m_pitch += dy;

	float limit = glm::radians(89.0f);
	m_pitch = glm::clamp(m_pitch, -limit, limit);
}

void Camera::pan(float dx, float dy) {
	// Scale pan by distance (Maya-ish)
	float s = m_distance * 0.0015f;
	m_target += (-right() * dx + up() * dy) * s;
}

void Camera::dolly(float dy) {
	float zoom = glm::exp(dy * 0.01f);
	m_distance *= zoom;
	m_distance = glm::max(0.01f, m_distance);
}

void Camera::rayFromPixel(float px, float py, glm::vec3& outOrigin, glm::vec3& outDir) const {
	// px,py are in viewport pixels with origin at top-left
	float x = (2.0f * (px + 0.5f) / (float)m_width) - 1.0f;
	float y = 1.0f - (2.0f * (py + 0.5f) / (float)m_height);

	glm::mat4 invVP = glm::inverse(viewProj());
	glm::vec4 nearP = invVP * glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 farP = invVP * glm::vec4(x, y, 1.0f, 1.0f);
	nearP /= nearP.w;
	farP /= farP.w;

	outOrigin = glm::vec3(nearP);
	outDir = glm::normalize(glm::vec3(farP - nearP));
}
