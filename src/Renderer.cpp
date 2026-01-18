#include "Renderer.h"

#include "Scene.h"
#include "Mesh.h"
#include "HairGuides.h"
#include "Camera.h"
#include "ImageLoader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cstdio>
#include <cstring>

static const char* kMeshVs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vNrm;
out vec3 vPos;
out vec2 vUv;

void main(){
	vec4 wp = uModel * vec4(aPos, 1.0);
	vPos = wp.xyz;
	vNrm = mat3(uModel) * aNrm;
	vUv = aUv;
	gl_Position = uViewProj * wp;
}
)";

static const char* kMeshFs = R"(
#version 330 core
in vec3 vNrm;
in vec3 vPos;
in vec2 vUv;
out vec4 oColor;

uniform vec3 uCamPos;
uniform int uUseTex;
uniform sampler2D uTex;

void main(){
	vec3 n = normalize(vNrm);
	// Light comes from the camera (headlight), like DCC viewports
	vec3 l = normalize(uCamPos - vPos);
	float ndl = max(dot(n, l), 0.0);
	vec3 v = normalize(uCamPos - vPos);
	vec3 h = normalize(l + v);
	float spec = pow(max(dot(n, h), 0.0), 48.0);
	vec3 base = vec3(0.55, 0.55, 0.56);
	vec3 albedo = (uUseTex != 0) ? texture(uTex, vUv).rgb : base;
	vec3 col = albedo * (0.25 + 0.75 * ndl) + vec3(0.10) * spec;
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

static const char* kHairVs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aTangent;
layout(location=2) in float aS;
layout(location=3) in float aSide;
layout(location=4) in float aLen;

uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uRootThickness;
uniform float uMidThickness;
uniform float uTipThickness;
uniform float uRootExtent;
uniform float uTipExtent;

out vec3 vPos;
out vec3 vNrm;

void main(){
	vec3 t = normalize(aTangent);
	vec3 viewDir = normalize(uCamPos - aPos);
	vec3 side = normalize(cross(viewDir, t));
	float rootExt = max(uRootExtent, 0.0);
	float tipExt = max(uTipExtent, 0.0);
	float len = max(aLen, 0.0001);
	float s = clamp(aS, 0.0, len);
	float width = uMidThickness;
	if (rootExt > 1e-6) {
		float rt = clamp(s / rootExt, 0.0, 1.0);
		width = mix(uRootThickness, uMidThickness, rt);
	} else {
		width = uMidThickness;
	}
	if (tipExt > 1e-6) {
		float tipStart = max(len - tipExt, 0.0);
		if (s >= tipStart) {
			float tt = clamp((s - tipStart) / tipExt, 0.0, 1.0);
			width = mix(uMidThickness, uTipThickness, tt);
		}
	}
	vec3 pos = aPos + side * (width * aSide);
	vPos = pos;
	vNrm = normalize(cross(t, side));
	gl_Position = uViewProj * vec4(pos, 1.0);
}
)";

static const char* kHairFs = R"(
#version 330 core
in vec3 vPos;
in vec3 vNrm;
out vec4 oColor;

uniform vec3 uCamPos;
uniform vec3 uHairColor;

void main(){
	vec3 n = normalize(vNrm);
	vec3 l = normalize(uCamPos - vPos);
	float ndl = max(dot(n, l), 0.0);
	vec3 v = normalize(uCamPos - vPos);
	vec3 h = normalize(l + v);
	float spec = pow(max(dot(n, h), 0.0), 32.0);
	vec3 col = uHairColor * (0.3 + 0.7 * ndl) + vec3(0.08) * spec;
	oColor = vec4(col, 1.0);
}
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
	m_hairProgram = createProgram(kHairVs, kHairFs);
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

bool Renderer::loadMeshTexture(const std::string& path) {
	clearMeshTexture();

	int w = 0, h = 0;
	std::vector<unsigned char> pixels;
	if (!ImageLoader::loadRGBA8(path, w, h, pixels) || w <= 0 || h <= 0 || pixels.empty()) {
		return false;
	}

	glGenTextures(1, &m_meshTexture);
	glBindTexture(GL_TEXTURE_2D, m_meshTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

void Renderer::clearMeshTexture() {
	if (m_meshTexture) {
		glDeleteTextures(1, &m_meshTexture);
		m_meshTexture = 0;
	}
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

static uint64_t hashCombine(uint64_t h, uint64_t v) {
	return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
}

static uint64_t hashFloat(float f) {
	uint32_t u = 0;
	std::memcpy(&u, &f, sizeof(float));
	return (uint64_t)u;
}

static uint64_t hashString(const std::string& s) {
	uint64_t h = 1469598103934665603ull;
	for (unsigned char c : s) {
		h ^= (uint64_t)c;
		h *= 1099511628211ull;
	}
	return h;
}

static uint64_t computeHairCacheKey(const Scene& scene) {
	uint64_t h = 0;
	const HairSettings& hs = scene.hairSettings();
	const GuideSettings& gs = scene.guideSettings();
	h = hashCombine(h, scene.meshVersion());
	h = hashCombine(h, scene.guides().version());
	h = hashCombine(h, (uint64_t)hs.hairCount);
	h = hashCombine(h, (uint64_t)hs.distribution);
	h = hashCombine(h, hashFloat(hs.rootThickness));
	h = hashCombine(h, hashFloat(hs.midThickness));
	h = hashCombine(h, hashFloat(hs.tipThickness));
	h = hashCombine(h, hashFloat(hs.rootExtent));
	h = hashCombine(h, hashFloat(hs.tipExtent));
	h = hashCombine(h, hashFloat(gs.defaultLength));
	h = hashCombine(h, (uint64_t)gs.defaultSteps);
	h = hashCombine(h, hashString(hs.distributionMaskPath));
	h = hashCombine(h, hashString(hs.lengthMaskPath));
	return h;
}

void Renderer::uploadHair(const Scene& scene) {
	const bool force = scene.guideSettings().enableSimulation || scene.isDragging();
	const uint64_t key = computeHairCacheKey(scene);
	if (!force && m_hairCacheValid && m_hairCacheKey == key) {
		return;
	}
	HairRenderData data;
	scene.buildHairRenderData(data);
	if (data.indices.empty() || data.vertices.empty()) {
		m_hairIndexCount = 0;
		m_hairCacheValid = true;
		m_hairCacheKey = key;
		return;
	}

	if (m_hairVao == 0) {
		glGenVertexArrays(1, &m_hairVao);
		glGenBuffers(1, &m_hairVbo);
		glGenBuffers(1, &m_hairEbo);
		glBindVertexArray(m_hairVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_hairVbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hairEbo);

		const int stride = 9 * sizeof(float);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
		glBindVertexArray(0);
	}

	glBindVertexArray(m_hairVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_hairVbo);
	glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(float), data.vertices.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hairEbo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.data(), GL_DYNAMIC_DRAW);
	glBindVertexArray(0);

	m_hairIndexCount = (int)data.indices.size();
	m_hairCacheKey = key;
	m_hairCacheValid = true;
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

		int useTex = (m_meshTexture != 0) ? 1 : 0;
		glUniform1i(glGetUniformLocation(m_meshProgram, "uUseTex"), useTex);
		if (useTex) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_meshTexture);
			glUniform1i(glGetUniformLocation(m_meshProgram, "uTex"), 0);
		}

		scene.mesh()->draw();
		if (useTex) {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glUseProgram(0);
	}

	if (rs.showGuides) {
		// Guides can be translucent (deselected opacity), so enable alpha blending.
		GLboolean wasBlend = glIsEnabled(GL_BLEND);
		GLboolean wasDepthMask = GL_TRUE;
		glGetBooleanv(GL_DEPTH_WRITEMASK, &wasDepthMask);
		GLint oldSrcRGB = 0, oldDstRGB = 0, oldSrcA = 0, oldDstA = 0;
		glGetIntegerv(GL_BLEND_SRC_RGB, &oldSrcRGB);
		glGetIntegerv(GL_BLEND_DST_RGB, &oldDstRGB);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &oldSrcA);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &oldDstA);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE);

		// Draw control points and interpolated lines
		const HairGuideSet& guides = scene.guides();
		guides.drawDebugLines(camera.viewProj(), m_lineProgram, rs.guidePointSizePx, rs.deselectedCurveOpacity, scene.hoverCurve(), scene.hoverHighlightActive());

		glDepthMask(wasDepthMask);
		glBlendFuncSeparate(oldSrcRGB, oldDstRGB, oldSrcA, oldDstA);
		if (!wasBlend) glDisable(GL_BLEND);
	}

	if (rs.showHair && scene.activeModule() == ModuleType::Hair) {
		uploadHair(scene);
		if (m_hairIndexCount > 0) {
			GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
			if (wasCull) glDisable(GL_CULL_FACE);

			glUseProgram(m_hairProgram);
			glUniformMatrix4fv(glGetUniformLocation(m_hairProgram, "uViewProj"), 1, GL_FALSE, glm::value_ptr(camera.viewProj()));
			glm::vec3 camPos = camera.position();
			glUniform3fv(glGetUniformLocation(m_hairProgram, "uCamPos"), 1, glm::value_ptr(camPos));
			const HairSettings& hs = scene.hairSettings();
			glUniform1f(glGetUniformLocation(m_hairProgram, "uRootThickness"), hs.rootThickness);
			glUniform1f(glGetUniformLocation(m_hairProgram, "uMidThickness"), hs.midThickness);
			glUniform1f(glGetUniformLocation(m_hairProgram, "uTipThickness"), hs.tipThickness);
			glUniform1f(glGetUniformLocation(m_hairProgram, "uRootExtent"), hs.rootExtent);
			glUniform1f(glGetUniformLocation(m_hairProgram, "uTipExtent"), hs.tipExtent);
			glUniform3f(glGetUniformLocation(m_hairProgram, "uHairColor"), 0.90f, 0.80f, 0.65f);

			glBindVertexArray(m_hairVao);
			glDrawElements(GL_TRIANGLES, m_hairIndexCount, GL_UNSIGNED_INT, nullptr);
			glBindVertexArray(0);
			glUseProgram(0);

			if (wasCull) glEnable(GL_CULL_FACE);
		}
	}
}
