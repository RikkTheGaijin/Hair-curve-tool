#include "Renderer.h"

#include "Scene.h"
#include "Mesh.h"
#include "HairGuides.h"
#include "Camera.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cstdio>

static const char* kMeshVs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vNrm;
out vec3 vPos;

void main(){
	vec4 wp = uModel * vec4(aPos, 1.0);
	vPos = wp.xyz;
	vNrm = mat3(uModel) * aNrm;
	gl_Position = uViewProj * wp;
}
)";

static const char* kMeshFs = R"(
#version 330 core
in vec3 vNrm;
in vec3 vPos;
out vec4 oColor;

uniform vec3 uCamPos;

void main(){
	vec3 n = normalize(vNrm);
	// Light comes from the camera (headlight), like DCC viewports
	vec3 l = normalize(uCamPos - vPos);
	float ndl = max(dot(n, l), 0.0);
	vec3 v = normalize(uCamPos - vPos);
	vec3 h = normalize(l + v);
	float spec = pow(max(dot(n, h), 0.0), 48.0);
	vec3 base = vec3(0.55, 0.55, 0.56);
	vec3 col = base * (0.25 + 0.75 * ndl) + vec3(0.10) * spec;
	oColor = vec4(col, 1.0);
}
)";

static const char* kLineVs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aCol;

uniform mat4 uViewProj;

out vec4 vCol;

void main(){
	vCol = aCol;
	gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

static const char* kLineFs = R"(
#version 330 core
in vec4 vCol;
out vec4 oColor;
void main(){ oColor = vCol; }
)";

static unsigned int compileShader(GLenum type, const char* src) {
	unsigned int s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	int ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(s, 4096, nullptr, log);
		std::fprintf(stderr, "Shader compile error: %s\n", log);
	}
	return s;
}

unsigned int Renderer::createProgram(const char* vs, const char* fs) {
	unsigned int p = glCreateProgram();
	unsigned int sv = compileShader(GL_VERTEX_SHADER, vs);
	unsigned int sf = compileShader(GL_FRAGMENT_SHADER, fs);
	glAttachShader(p, sv);
	glAttachShader(p, sf);
	glLinkProgram(p);
	glDeleteShader(sv);
	glDeleteShader(sf);
	int ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetProgramInfoLog(p, 4096, nullptr, log);
		std::fprintf(stderr, "Program link error: %s\n", log);
	}
	return p;
}

void Renderer::createPrograms() {
	m_meshProgram = createProgram(kMeshVs, kMeshFs);
	m_lineProgram = createProgram(kLineVs, kLineFs);
}

void Renderer::createGrid() {
	// Maya-ish ground grid centered at origin
	std::vector<float> verts; // pos3 + col4
	const int lines = 40;
	const float half = 1.0f;
	const float step = (2.0f * half) / (float)lines;

	auto push = [&](glm::vec3 p, glm::vec4 c) {
		verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
		verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
	};

	glm::vec4 major(0.35f, 0.35f, 0.35f, 1.0f);
	glm::vec4 minor(0.22f, 0.22f, 0.22f, 1.0f);

	for (int i = 0; i <= lines; i++) {
		float x = -half + i * step;
		glm::vec4 c = (i % 5 == 0) ? major : minor;
		push({x, 0.0f, -half}, c);
		push({x, 0.0f,  half}, c);
		push({-half, 0.0f, x}, c);
		push({ half, 0.0f, x}, c);
	}

	m_gridVertexCount = (int)(verts.size() / 7);

	glGenVertexArrays(1, &m_gridVao);
	glBindVertexArray(m_gridVao);
	glGenBuffers(1, &m_gridVbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
	glBindVertexArray(0);
}

void Renderer::init() {
	createPrograms();
	createGrid();
}

void Renderer::drawGrid(const Camera& camera, const glm::vec3& center, float scale) const {
	(void)center;
	(void)scale;
	glUseProgram(m_lineProgram);
	glUniformMatrix4fv(glGetUniformLocation(m_lineProgram, "uViewProj"), 1, GL_FALSE, glm::value_ptr(camera.viewProj()));
	glBindVertexArray(m_gridVao);
	glDrawArrays(GL_LINES, 0, m_gridVertexCount);
	glBindVertexArray(0);
	glUseProgram(0);
}

void Renderer::render(const Scene& scene, const Camera& camera) {
	const RenderSettings& rs = scene.renderSettings();

	if (rs.showGrid) {
		drawGrid(camera, glm::vec3(0), 1.0f);
	}

	if (rs.showMesh && scene.mesh()) {
		glUseProgram(m_meshProgram);
		glUniformMatrix4fv(glGetUniformLocation(m_meshProgram, "uViewProj"), 1, GL_FALSE, glm::value_ptr(camera.viewProj()));
		glm::mat4 model(1.0f);
		glUniformMatrix4fv(glGetUniformLocation(m_meshProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
		glm::vec3 camPos = camera.position();
		glUniform3fv(glGetUniformLocation(m_meshProgram, "uCamPos"), 1, glm::value_ptr(camPos));
		scene.mesh()->draw();
		glUseProgram(0);
	}

	if (rs.showGuides) {
		// Draw control points and interpolated lines
		const HairGuideSet& guides = scene.guides();
		guides.drawDebugLines(camera.viewProj(), m_lineProgram, rs.guidePointSizePx, scene.hoverCurve(), scene.hoverHighlightActive());
	}
}
