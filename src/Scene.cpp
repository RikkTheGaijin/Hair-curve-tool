#include "Scene.h"

#include "Mesh.h"
#include "Raycast.h"
#include "Physics.h"
#include "GpuSolver.h"
#include "MayaCameraController.h"
#include "ImageLoader.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <numeric>
#include <limits>

glm::vec3 Scene::mirrorX(const glm::vec3& p) {
	return glm::vec3(-p.x, p.y, p.z);
}

void Scene::clearMirrorPairs() {
	m_mirrorPeer.clear();
}

int Scene::mirrorPeerOf(int curveIdx) const {
	auto it = m_mirrorPeer.find(curveIdx);
	if (it == m_mirrorPeer.end()) return -1;
	return it->second;
}

void Scene::setMirrorPair(int a, int b) {
	if (a < 0 || b < 0 || a == b) return;
	m_mirrorPeer[a] = b;
	m_mirrorPeer[b] = a;
}

void Scene::clearMirrorPairFor(int curveIdx) {
	auto it = m_mirrorPeer.find(curveIdx);
	if (it == m_mirrorPeer.end()) return;
	int other = it->second;
	m_mirrorPeer.erase(curveIdx);
	if (other >= 0) {
		auto it2 = m_mirrorPeer.find(other);
		if (it2 != m_mirrorPeer.end() && it2->second == curveIdx) {
			m_mirrorPeer.erase(it2);
		}
	}
}

void Scene::pruneMirrorPairsToSelection() {
	// Mirror mode only applies while both curves in the pair remain selected.
	for (auto it = m_mirrorPeer.begin(); it != m_mirrorPeer.end(); ) {
		int a = it->first;
		int b = it->second;
		bool aSel = (a >= 0) ? m_guides.isCurveSelected((size_t)a) : false;
		bool bSel = (b >= 0) ? m_guides.isCurveSelected((size_t)b) : false;
		if (!aSel || !bSel) {
			int key = a;
			++it;
			clearMirrorPairFor(key);
			continue;
		}
		++it;
	}
}

Scene::Scene() {
	resetLayers();
	// Maya-ish defaults
	m_guideSettings.defaultLength = 0.3f;
	m_guideSettings.defaultSteps = 12;
	m_guideSettings.enableSimulation = true;  // Physics enabled by default
	m_guideSettings.enableGpuSolver = false;  // CPU solver default (CUDA can be toggled on)
	m_guideSettings.enableMeshCollision = true;  // Re-enable collision with fixed physics
	m_guideSettings.enableCurveCollision = false;
	m_guideSettings.collisionFriction = 1.0f;
	// Collision thickness in meters
	m_guideSettings.collisionThickness = 0.0020f;
	// Default forces/constraints
	m_guideSettings.gravity = 0.0f;
	m_guideSettings.damping = 0.900f;
	m_guideSettings.stiffness = 0.10f;
	// More iterations helps stabilize when manually dragging vertices
	m_guideSettings.solverIterations = 24;
}

void Scene::resetLayers() {
	m_layers.clear();
	LayerInfo base;
	base.name = "Layer 0";
	base.color = glm::vec3(0.90f, 0.75f, 0.22f);
	base.visible = true;
	m_layers.push_back(base);
	m_activeLayer = 0;
}

void Scene::setLayers(const std::vector<LayerInfo>& layers, int activeLayer) {
	if (layers.empty()) {
		resetLayers();
		return;
	}
	m_layers = layers;
	if (activeLayer < 0 || activeLayer >= (int)m_layers.size()) {
		m_activeLayer = 0;
	} else {
		m_activeLayer = activeLayer;
	}
	refreshCurveLayerProperties();
}

int Scene::addLayer(const std::string& name, const glm::vec3& color, bool visible) {
	LayerInfo l;
	l.name = name.empty() ? (std::string("Layer ") + std::to_string(m_layers.size())) : name;
	l.color = color;
	l.visible = visible;
	m_layers.push_back(l);
	return (int)m_layers.size() - 1;
}

bool Scene::deleteLayer(int layerId) {
	if (layerId <= 0 || layerId >= (int)m_layers.size()) return false;

	// Remove curves belonging to this layer
	std::vector<int> toRemove;
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		if (m_guides.curve(ci).layerId == layerId) {
			toRemove.push_back((int)ci);
		}
	}
	if (!toRemove.empty()) {
		std::sort(toRemove.begin(), toRemove.end());
		std::reverse(toRemove.begin(), toRemove.end());
		m_guides.removeCurves(toRemove);
	}

	m_layers.erase(m_layers.begin() + layerId);

	// Adjust curve layer indices after removal
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		HairCurve& c = m_guides.curve(ci);
		if (c.layerId > layerId) c.layerId--;
	}

	if (m_activeLayer == layerId) {
		m_activeLayer = 0;
	} else if (m_activeLayer > layerId) {
		m_activeLayer--;
	}

	refreshCurveLayerProperties();
	m_guides.deselectAll();
	clearMirrorPairs();
	m_hoverCurve = -1;
	m_hoverHighlightActive = false;
	endDragVertex();
	return true;
}

void Scene::setActiveLayer(int layerId) {
	if (layerId < 0 || layerId >= (int)m_layers.size()) return;
	if (m_activeLayer == layerId) return;
	m_activeLayer = layerId;
	m_guides.deselectAll();
	clearMirrorPairs();
	m_hoverCurve = -1;
	m_hoverHighlightActive = false;
	endDragVertex();
}

void Scene::setLayerVisible(int layerId, bool visible) {
	if (layerId < 0 || layerId >= (int)m_layers.size()) return;
	m_layers[(size_t)layerId].visible = visible;
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		HairCurve& c = m_guides.curve(ci);
		if (c.layerId == layerId) {
			c.visible = visible;
		}
	}
}

void Scene::setLayerColor(int layerId, const glm::vec3& color) {
	if (layerId < 0 || layerId >= (int)m_layers.size()) return;
	m_layers[(size_t)layerId].color = color;
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		HairCurve& c = m_guides.curve(ci);
		if (c.layerId == layerId) {
			c.color = color;
		}
	}
}

bool Scene::isLayerVisible(int layerId) const {
	if (layerId < 0 || layerId >= (int)m_layers.size()) return false;
	return m_layers[(size_t)layerId].visible;
}

static glm::vec3 hsvToRgb(float h, float s, float v) {
	float r = v, g = v, b = v;
	if (s > 0.0f) {
		h = std::fmod(h, 1.0f) * 6.0f;
		int i = (int)std::floor(h);
		float f = h - (float)i;
		float p = v * (1.0f - s);
		float q = v * (1.0f - s * f);
		float t = v * (1.0f - s * (1.0f - f));
		switch (i) {
			case 0: r = v; g = t; b = p; break;
			case 1: r = q; g = v; b = p; break;
			case 2: r = p; g = v; b = t; break;
			case 3: r = p; g = q; b = v; break;
			case 4: r = t; g = p; b = v; break;
			default: r = v; g = p; b = q; break;
		}
	}
	return glm::vec3(r, g, b);
}

static float sampleMaskValue(const MaskData& mask, const glm::vec2& uv) {
	if (!mask.valid()) return 1.0f;
	float u = glm::clamp(uv.x, 0.0f, 1.0f);
	float v = glm::clamp(uv.y, 0.0f, 1.0f);
	int x = (int)std::round(u * (float)(mask.w - 1));
	int y = (int)std::round(v * (float)(mask.h - 1));
	int idx = (y * mask.w + x) * 4;
	if (idx < 0 || idx + 2 >= (int)mask.pixels.size()) return 1.0f;
	unsigned char b = mask.pixels[(size_t)idx + 0u];
	unsigned char g = mask.pixels[(size_t)idx + 1u];
	unsigned char r = mask.pixels[(size_t)idx + 2u];
	float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
	// Treat white as full effect, black as none (no inversion).
	return glm::clamp(lum, 0.0f, 1.0f);
}

struct HairRootSample {
	int triIndex = -1;
	glm::vec3 bary{0.0f};
	glm::vec3 pos{0.0f};
	glm::vec3 nrm{0.0f, 1.0f, 0.0f};
	glm::vec2 uv{0.0f};
};

static bool meshHasUvs(const Mesh& mesh) {
	return !mesh.uvs().empty();
}

static bool meshHasNormals(const Mesh& mesh) {
	return !mesh.normals().empty();
}

static HairRootSample sampleTriangle(const Mesh& mesh, int triIndex, float r1, float r2) {
	HairRootSample s;
	s.triIndex = triIndex;

	const auto& idx = mesh.indices();
	const auto& pos = mesh.positions();
	const auto& nrm = mesh.normals();
	const auto& uv = mesh.uvs();

	unsigned int i0 = idx[(size_t)triIndex * 3u + 0u];
	unsigned int i1 = idx[(size_t)triIndex * 3u + 1u];
	unsigned int i2 = idx[(size_t)triIndex * 3u + 2u];

	glm::vec3 p0 = pos[i0];
	glm::vec3 p1 = pos[i1];
	glm::vec3 p2 = pos[i2];

	if (r1 + r2 > 1.0f) {
		r1 = 1.0f - r1;
		r2 = 1.0f - r2;
	}
	float w0 = 1.0f - r1 - r2;
	float w1 = r1;
	float w2 = r2;
	s.bary = glm::vec3(w0, w1, w2);
	s.pos = p0 * w0 + p1 * w1 + p2 * w2;

	if (meshHasUvs(mesh)) {
		glm::vec2 uv0 = uv[i0];
		glm::vec2 uv1 = uv[i1];
		glm::vec2 uv2 = uv[i2];
		s.uv = uv0 * w0 + uv1 * w1 + uv2 * w2;
	}

	if (meshHasNormals(mesh)) {
		glm::vec3 n0 = nrm[i0];
		glm::vec3 n1 = nrm[i1];
		glm::vec3 n2 = nrm[i2];
		s.nrm = glm::normalize(n0 * w0 + n1 * w1 + n2 * w2);
	} else {
		s.nrm = glm::normalize(glm::cross(p1 - p0, p2 - p0));
	}

	return s;
}

static HairRootSample sampleTriangleBary(const Mesh& mesh, int triIndex, const glm::vec3& bary) {
	HairRootSample s;
	s.triIndex = triIndex;

	const auto& idx = mesh.indices();
	const auto& pos = mesh.positions();
	const auto& nrm = mesh.normals();
	const auto& uv = mesh.uvs();

	unsigned int i0 = idx[(size_t)triIndex * 3u + 0u];
	unsigned int i1 = idx[(size_t)triIndex * 3u + 1u];
	unsigned int i2 = idx[(size_t)triIndex * 3u + 2u];

	glm::vec3 p0 = pos[i0];
	glm::vec3 p1 = pos[i1];
	glm::vec3 p2 = pos[i2];

	glm::vec3 b = bary;
	b = glm::clamp(b, glm::vec3(0.0f), glm::vec3(1.0f));
	float ssum = b.x + b.y + b.z;
	if (ssum <= 1e-8f) b = glm::vec3(1.0f, 0.0f, 0.0f);
	else b /= ssum;

	s.bary = b;
	s.pos = p0 * b.x + p1 * b.y + p2 * b.z;

	if (meshHasUvs(mesh)) {
		glm::vec2 uv0 = uv[i0];
		glm::vec2 uv1 = uv[i1];
		glm::vec2 uv2 = uv[i2];
		s.uv = uv0 * b.x + uv1 * b.y + uv2 * b.z;
	}

	if (meshHasNormals(mesh)) {
		glm::vec3 n0 = nrm[i0];
		glm::vec3 n1 = nrm[i1];
		glm::vec3 n2 = nrm[i2];
		s.nrm = glm::normalize(n0 * b.x + n1 * b.y + n2 * b.z);
	} else {
		s.nrm = glm::normalize(glm::cross(p1 - p0, p2 - p0));
	}

	return s;
}

static bool barycentric2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, glm::vec3& outBary) {
	glm::vec2 v0 = b - a;
	glm::vec2 v1 = c - a;
	glm::vec2 v2 = p - a;
	float d00 = glm::dot(v0, v0);
	float d01 = glm::dot(v0, v1);
	float d11 = glm::dot(v1, v1);
	float d20 = glm::dot(v2, v0);
	float d21 = glm::dot(v2, v1);
	float denom = d00 * d11 - d01 * d01;
	if (glm::abs(denom) < 1e-12f) return false;
	float v = (d11 * d20 - d01 * d21) / denom;
	float w = (d00 * d21 - d01 * d20) / denom;
	float u = 1.0f - v - w;
	outBary = glm::vec3(u, v, w);
	return (u >= -1e-4f && v >= -1e-4f && w >= -1e-4f);
}

static float triArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
	return 0.5f * glm::length(glm::cross(b - a, c - a));
}

static glm::vec3 sampleCurveAtLength(const HairCurve& c, float length) {
	if (c.points.empty()) return glm::vec3(0.0f);
	if (c.points.size() == 1) return c.points[0];

	float remaining = glm::max(0.0f, length);
	for (size_t i = 0; i + 1 < c.points.size(); i++) {
		glm::vec3 a = c.points[i];
		glm::vec3 b = c.points[i + 1];
		float seg = glm::length(b - a);
		if (remaining <= seg || i + 1 == c.points.size() - 1) {
			float t = (seg > 1e-6f) ? (remaining / seg) : 0.0f;
			return glm::mix(a, b, t);
		}
		remaining -= seg;
	}
	return c.points.back();
}

static bool loadMaskData(const std::string& path, MaskData& outMask) {
	outMask = MaskData();
	if (path.empty()) return false;
	int w = 0, h = 0;
	std::vector<unsigned char> pixels;
	if (!ImageLoader::loadRGBA8(path, w, h, pixels) || w <= 0 || h <= 0 || pixels.empty()) return false;
	outMask.w = w;
	outMask.h = h;
	outMask.pixels = std::move(pixels);
	return outMask.valid();
}

glm::vec3 Scene::generateDistinctLayerColor() {
	std::mt19937 rng((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
	std::uniform_real_distribution<float> dist(0.1f, 0.95f);

	auto isDistinct = [&](const glm::vec3& c) {
		const float minDist = 0.35f;
		for (const auto& layer : m_layers) {
			float d = glm::length(c - layer.color);
			if (d < minDist) return false;
		}
		return true;
	};

	for (int i = 0; i < 32; i++) {
		glm::vec3 c(dist(rng), dist(rng), dist(rng));
		if (isDistinct(c)) return c;
	}

	// Fallback: golden-ratio hue sweep for distinctness
	float h = std::fmod((float)m_layers.size() * 0.61803398875f, 1.0f);
	return hsvToRgb(h, 0.65f, 0.95f);
}

void Scene::refreshCurveLayerProperties() {
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		HairCurve& c = m_guides.curve(ci);
		if (c.layerId < 0 || c.layerId >= (int)m_layers.size()) c.layerId = 0;
		const LayerInfo& layer = m_layers[(size_t)c.layerId];
		c.color = layer.color;
		c.visible = layer.visible;
	}
}

void Scene::resetSettingsToDefaults() {
	// Keep mesh + curves; reset only user-tweakable settings.
	m_guideSettings = GuideSettings();
	// Re-apply the same defaults as the constructor (which are not the same as struct defaults)
	m_guideSettings.defaultLength = 0.3f;
	m_guideSettings.defaultSteps = 12;
	m_guideSettings.enableSimulation = true;
	m_guideSettings.enableGpuSolver = false;
	m_guideSettings.enableMeshCollision = true;
	m_guideSettings.enableCurveCollision = false;
	m_guideSettings.collisionFriction = 1.0f;
	m_guideSettings.collisionThickness = 0.0020f;
	m_guideSettings.gravity = 0.0f;
	m_guideSettings.damping = 0.900f;
	m_guideSettings.stiffness = 0.10f;
	m_guideSettings.solverIterations = 24;

	m_renderSettings = RenderSettings();
	m_hairSettings = HairSettings();
	clearHairMasks();
}

bool Scene::loadHairDistributionMask(const std::string& path) {
	if (path.empty()) {
		m_distMask = MaskData();
		m_hairSettings.distributionMaskPath.clear();
		return true;
	}
	if (!loadMaskData(path, m_distMask)) return false;
	m_hairSettings.distributionMaskPath = path;
	return true;
}

bool Scene::loadHairLengthMask(const std::string& path) {
	if (path.empty()) {
		m_lenMask = MaskData();
		m_hairSettings.lengthMaskPath.clear();
		return true;
	}
	if (!loadMaskData(path, m_lenMask)) return false;
	m_hairSettings.lengthMaskPath = path;
	return true;
}

void Scene::clearHairMasks() {
	m_distMask = MaskData();
	m_lenMask = MaskData();
	m_hairSettings.distributionMaskPath.clear();
	m_hairSettings.lengthMaskPath.clear();
}

bool Scene::loadMeshFromObj(const std::string& path) {
	m_mesh = std::make_unique<Mesh>();
	if (!m_mesh->loadFromObj(path)) {
		m_mesh.reset();
		return false;
	}
	m_meshPath = path;
	m_meshTexturePath.clear();
	clearMirrorPairs();
	m_meshBoundsMin = m_mesh->boundsMin();
	m_meshBoundsMax = m_mesh->boundsMax();
	m_meshVersion++;
	// Distance field will be built lazily when GPU solver is first used
	m_meshField.clear();
	m_guides.clear();
	return true;
}

void Scene::clearCurves() {
	m_guides.clear();
	clearMirrorPairs();
	m_hoverCurve = -1;
	m_hoverHighlightActive = false;
	m_dragCurve = -1;
	m_dragVert = -1;
	m_dragging = false;
}

void Scene::tick() {
	// Remove zero-length curves to prevent export issues.
	std::vector<int> toRemove;
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		const HairCurve& c = m_guides.curve(ci);
		if (c.points.size() < 2) {
			toRemove.push_back((int)ci);
			continue;
		}
		float sum = 0.0f;
		for (size_t i = 0; i + 1 < c.points.size(); i++) {
			sum += glm::length(c.points[i + 1] - c.points[i]);
		}
		if (sum <= 1e-6f) {
			toRemove.push_back((int)ci);
		}
	}
	if (!toRemove.empty()) {
		std::sort(toRemove.begin(), toRemove.end());
		toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
		std::reverse(toRemove.begin(), toRemove.end());
		m_guides.removeCurves(toRemove);
		clearMirrorPairs();
		m_guides.deselectAll();
		m_hoverCurve = -1;
		m_hoverHighlightActive = false;
		endDragVertex();
	}
}

void Scene::simulate(float dt) {
	if (!m_guideSettings.enableSimulation) return;

	// Fixed timestep accumulator (spiderweb-style). This makes the solver behavior
	// stable under variable frame rate and avoids energy injection from large dt spikes.
	static float accumulator = 0.0f;
	const float fixedDt = 1.0f / 120.0f;
	const float maxFrameDt = 1.0f / 15.0f; // clamp huge hitches
	const float clampedDt = glm::clamp(dt, 0.0f, maxFrameDt);
	accumulator += clampedDt;

	// Prevent spiral-of-death if the app stalls.
	const int maxStepsPerFrame = 8;
	int steps = 0;
	while (accumulator >= fixedDt && steps < maxStepsPerFrame) {
		if (m_guideSettings.enableGpuSolver && GpuSolver::isAvailable()) {
			GpuSolver::step(*this, fixedDt);
		} else {
			Physics::step(*this, fixedDt);
		}
		accumulator -= fixedDt;
		steps++;
	}
}

static bool intersectRayPlane(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& p0, const glm::vec3& n, float& t) {
	float denom = glm::dot(n, rd);
	if (glm::abs(denom) < 1e-6f) return false;
	t = glm::dot(p0 - ro, n) / denom;
	return t >= 0.0f;
}

void Scene::handleViewportMouse(const MayaCameraController& camera, int viewportW, int viewportH) {
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse) return;

	m_hoverCurve = -1;
	m_hoverHighlightActive = false;

	// Hover highlight for selection
	if (io.KeyShift) {
		ImVec2 mouse = ImGui::GetMousePos();
		float px = mouse.x;
		float py = mouse.y;
		if (px >= 0 && py >= 0 && px < viewportW && py < viewportH) {
			glm::vec3 ro, rd;
			camera.rayFromPixel(px, py, ro, rd);
			int hc = -1;
			if (m_guides.pickCurve(ro, rd, hc, m_activeLayer, true)) {
				m_hoverCurve = hc;
				m_hoverHighlightActive = true;
			}
		}
	}

	// SHIFT+MMB on empty space deselects all
	if (io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
		if (m_hoverCurve < 0) {
			m_guides.deselectAll();
			clearMirrorPairs();
		}
		// In either case, don't treat SHIFT+MMB as curve creation.
		return;
	}

	// SHIFT+LMB selects a curve (single selection)
	// SHIFT+CTRL+LMB adds to selection (and makes it active)
	if (io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		if (m_hoverCurve >= 0) {
			const bool additive = io.KeyCtrl;
			m_guides.selectCurve(m_hoverCurve, additive);
			pruneMirrorPairsToSelection();
		}
		return;
	}

	// LMB edits selected curves only
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		beginDragVertex(camera, viewportW, viewportH);
	}
	if (m_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		updateDragVertex(camera, viewportW, viewportH);
	}
	if (m_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		endDragVertex();
	}

	// MMB creates a new curve (and selects it, deselecting others)
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
		// Don't create while dragging
		if (m_dragging) return;
		if (!m_mesh) return;

		ImVec2 mouse = ImGui::GetMousePos();
		float px = mouse.x;
		float py = mouse.y;
		if (px < 0 || py < 0 || px >= viewportW || py >= viewportH) return;

		glm::vec3 ro, rd;
		camera.rayFromPixel(px, py, ro, rd);
		RayHit hit;
		if (!Raycast::raycastMesh(*m_mesh, ro, rd, hit)) return;

		// Prevent duplicate roots (debounce double-clicks / overlapping curves)
		const float dupRootTol = std::max(0.0005f, m_guideSettings.collisionThickness * 0.5f);
		for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
			const HairCurve& c = m_guides.curve(ci);
			if (c.points.empty()) continue;
			if (glm::length(c.points[0] - hit.position) <= dupRootTol) {
				return;
			}
		}

		const LayerInfo& layer = m_layers[(size_t)m_activeLayer];
		int newIdx = m_guides.addCurveOnMesh(*m_mesh, hit.triIndex, hit.bary, hit.position, hit.normal, m_guideSettings, m_activeLayer, layer.color, layer.visible);
		if (newIdx >= 0) {
			// Mirror mode: only affects newly created curves while it is enabled.
			const bool mirrorOn = m_guideSettings.mirrorMode;
			if (mirrorOn && glm::abs(hit.position.x) > 1e-5f) {
				RayHit mh;
				glm::vec3 mp = mirrorX(hit.position);
				if (Raycast::nearestOnMesh(*m_mesh, mp, mh)) {
					// Prevent duplicate roots for mirror curve too
					for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
						const HairCurve& c = m_guides.curve(ci);
						if (c.points.empty()) continue;
						if (glm::length(c.points[0] - mh.position) <= dupRootTol) {
							return;
						}
					}
					int mirrorIdx = m_guides.addCurveOnMesh(*m_mesh, mh.triIndex, mh.bary, mh.position, mh.normal, m_guideSettings, m_activeLayer, layer.color, layer.visible);
					if (mirrorIdx >= 0) {
						// Mirror the initial shape 1:1 across the plane X=0.
						const HairCurve& src = m_guides.curve((size_t)newIdx);
						HairCurve& dst = m_guides.curve((size_t)mirrorIdx);
						const size_t n = glm::min(src.points.size(), dst.points.size());
						for (size_t i = 0; i < n; i++) {
							glm::vec3 p = mirrorX(src.points[i]);
							dst.points[i] = p;
							dst.prevPoints[i] = p;
						}
						dst.segmentRestLen = src.segmentRestLen;
						setMirrorPair(newIdx, mirrorIdx);

						// Select both, but keep the clicked curve as active.
						m_guides.selectCurve(newIdx, false);
						m_guides.selectCurve(mirrorIdx, true);
						m_guides.selectCurve(newIdx, true);
						return;
					}
				}
			}

			m_guides.selectCurve(newIdx, false);
			pruneMirrorPairsToSelection();
		}
	}
}

float Scene::effectiveGravityForCurve(size_t curveIdx) const {
	float g = m_guideSettings.gravity;
	if (!m_gravityOverrideHeld) return g;

	int active = m_guides.activeCurve();
	// If there's an active curve, only it gets the override.
	// If not, apply to all selected curves (more intuitive than doing nothing).
	if (active >= 0) {
		if ((int)curveIdx == active) return m_gravityOverrideValue;
		if (m_guideSettings.mirrorMode) {
			int peer = mirrorPeerOf(active);
			if (peer >= 0 && (int)curveIdx == peer) {
				return m_gravityOverrideValue;
			}
		}
		return g;
	}
	return m_gravityOverrideValue;
}

void Scene::beginDragVertex(const MayaCameraController& camera, int viewportW, int viewportH) {
	m_dragCurve = -1;
	m_dragVert = -1;
	m_dragging = false;

	// Mouse position in viewport (entire window is now viewport)
	ImVec2 mouse = ImGui::GetMousePos();
	float px = mouse.x;
	float py = mouse.y;
	if (px < 0 || py < 0 || px >= viewportW || py >= viewportH) return;

	glm::vec3 ro, rd;
	camera.rayFromPixel(px, py, ro, rd);

	// 1) Try picking a control vertex
	if (m_guides.pickControlPoint(ro, rd, camera.position(), camera.viewProj(), m_dragCurve, m_dragVert, true, m_activeLayer, true)) {
		// Ensure the dragged curve is active (without changing multi-selection)
		m_guides.selectCurve(m_dragCurve, true);
		m_dragging = true;
		glm::vec3 p = m_guides.curve(m_dragCurve).points[(size_t)m_dragVert];
		m_dragPlanePoint = p;
		m_dragPlaneNormal = camera.forward();
		return;
	}
}

void Scene::updateDragVertex(const MayaCameraController& camera, int viewportW, int viewportH) {
	if (m_dragCurve < 0 || m_dragVert < 0) return;
	if ((size_t)m_dragCurve >= m_guides.curveCount()) return;

	ImVec2 mouse = ImGui::GetMousePos();
	float px = mouse.x;
	float py = mouse.y;
	if (px < 0 || py < 0 || px >= viewportW || py >= viewportH) return;

	glm::vec3 ro, rd;
	camera.rayFromPixel(px, py, ro, rd);

	float t = 0.0f;
	if (!intersectRayPlane(ro, rd, m_dragPlanePoint, m_dragPlaneNormal, t)) return;
	glm::vec3 p = ro + rd * t;

	// Smooth dragging to reduce jitter (and avoid injecting energy into the solver)
	float a = glm::clamp(m_guideSettings.dragLerp, 0.05f, 1.0f);
	const HairCurve& c = m_guides.curve((size_t)m_dragCurve);
	if ((size_t)m_dragVert < c.points.size()) {
		p = glm::mix(c.points[(size_t)m_dragVert], p, a);
	}

	m_guides.moveControlPoint(m_dragCurve, m_dragVert, p);

	// Mirror dragging (only while both curves stay selected).
	if (m_guideSettings.mirrorMode) {
		int peer = mirrorPeerOf(m_dragCurve);
		if (peer >= 0) {
			bool peerSel = m_guides.isCurveSelected((size_t)peer);
			bool selfSel = m_guides.isCurveSelected((size_t)m_dragCurve);
			if (!peerSel || !selfSel) {
				clearMirrorPairFor(m_dragCurve);
			} else {
				glm::vec3 mp = mirrorX(p);
				m_guides.moveControlPoint(peer, m_dragVert, mp);
			}
		}
	}
}

void Scene::endDragVertex() {
	m_dragging = false;
	m_dragCurve = -1;
	m_dragVert = -1;
}

void Scene::deleteSelectedCurves() {
	// Deleting curves changes indices; mirror pairs are transient anyway.
	clearMirrorPairs();
	std::vector<int> sel = m_guides.selectedCurves();
	if (sel.empty()) return;
	// Remove in descending order to keep indices stable.
	std::sort(sel.begin(), sel.end());
	std::reverse(sel.begin(), sel.end());

	// If we're dragging a curve being deleted, stop dragging.
	if (m_dragging) {
		for (int idx : sel) {
			if (idx == m_dragCurve) {
				endDragVertex();
				break;
			}
		}
	}

	m_guides.removeCurves(sel);
}

void Scene::buildHairRenderData(HairRenderData& out) const {
	out.vertices.clear();
	out.indices.clear();
	m_lastHairCount = 0;

	if (!m_mesh) return;
	if (m_hairSettings.hairCount <= 0) return;

	const Mesh& mesh = *m_mesh;
	const auto& positions = mesh.positions();
	const auto& indices = mesh.indices();
	if (positions.empty() || indices.size() < 3) return;
	const size_t triCount = indices.size() / 3u;
	if (triCount == 0) return;

	std::mt19937 rng(1337u);
	std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

	// Build triangle area CDF for sampling
	std::vector<float> areas(triCount, 0.0f);
	float totalArea = 0.0f;
	for (size_t ti = 0; ti < triCount; ti++) {
		unsigned int i0 = indices[ti * 3u + 0u];
		unsigned int i1 = indices[ti * 3u + 1u];
		unsigned int i2 = indices[ti * 3u + 2u];
		float a = triArea(positions[i0], positions[i1], positions[i2]);
		areas[ti] = a;
		totalArea += a;
	}
	if (totalArea <= 1e-8f) return;
	std::vector<float> cdf(triCount, 0.0f);
	float accum = 0.0f;
	for (size_t i = 0; i < triCount; i++) {
		accum += areas[i] / totalArea;
		cdf[i] = accum;
	}

	auto pickTriangle = [&](float r) -> int {
		auto it = std::lower_bound(cdf.begin(), cdf.end(), r);
		if (it == cdf.end()) return (int)(cdf.size() - 1);
		return (int)std::distance(cdf.begin(), it);
	};

	auto sampleRandomRoot = [&]() -> HairRootSample {
		float r = dist01(rng);
		int tri = pickTriangle(r);
		float r1 = dist01(rng);
		float r2 = dist01(rng);
		return sampleTriangle(mesh, tri, r1, r2);
	};

	std::vector<HairRootSample> roots;
	roots.reserve((size_t)m_hairSettings.hairCount);

	int targetCount = m_hairSettings.hairCount;
	const int maxAttempts = glm::max(targetCount * 200, 5000);

	const float spacing = glm::max(std::sqrt(totalArea / (float)glm::max(targetCount, 1)), 1e-5f);

	if (m_hairSettings.distribution == HairDistributionType::Vertex) {
		const size_t vcount = positions.size();
		if (vcount == 0) return;
		targetCount = (int)vcount;
		int attempts = 0;
		while ((int)roots.size() < targetCount && attempts < maxAttempts) {
			size_t vi = (size_t)(dist01(rng) * (float)vcount) % vcount;
			HairRootSample s;
			s.pos = positions[vi];
			if (meshHasNormals(mesh)) s.nrm = glm::normalize(mesh.normals()[vi]);
			if (meshHasUvs(mesh)) s.uv = mesh.uvs()[vi];
			float mask = sampleMaskValue(m_distMask, s.uv);
			if (mask <= 0.0f) { attempts++; continue; }
			if (dist01(rng) > mask) { attempts++; continue; }
			roots.push_back(s);
			attempts++;
		}
	} else if (m_hairSettings.distribution == HairDistributionType::Even) {
		const float cellSize = spacing;
		if (cellSize <= 1e-6f) return;
		struct CellKey {
			int x = 0, y = 0, z = 0;
		};
		auto hashCell = [](int x, int y, int z) -> uint64_t {
			uint64_t h = 1469598103934665603ull;
			h ^= (uint64_t)(x * 73856093); h *= 1099511628211ull;
			h ^= (uint64_t)(y * 19349663); h *= 1099511628211ull;
			h ^= (uint64_t)(z * 83492791); h *= 1099511628211ull;
			return h;
		};
		std::unordered_map<uint64_t, std::vector<int>> grid;
		auto cellOf = [&](const glm::vec3& p) -> CellKey {
			return CellKey{
				(int)std::floor(p.x / cellSize),
				(int)std::floor(p.y / cellSize),
				(int)std::floor(p.z / cellSize)
			};
		};
		auto canPlace = [&](const glm::vec3& p) -> bool {
			CellKey c = cellOf(p);
			for (int dz = -1; dz <= 1; dz++) {
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						uint64_t h = hashCell(c.x + dx, c.y + dy, c.z + dz);
						auto it = grid.find(h);
						if (it == grid.end()) continue;
						for (int idx : it->second) {
							float d = glm::length(p - roots[(size_t)idx].pos);
							if (d < spacing) return false;
						}
					}
				}
			}
			return true;
		};
		auto addToGrid = [&](int idx) {
			CellKey c = cellOf(roots[(size_t)idx].pos);
			uint64_t h = hashCell(c.x, c.y, c.z);
			grid[h].push_back(idx);
		};

		int attempts = 0;
		int maxEvenAttempts = glm::max(targetCount * 20, 2000);
		while ((int)roots.size() < targetCount && attempts < maxEvenAttempts) {
			HairRootSample s = sampleRandomRoot();
			float mask = sampleMaskValue(m_distMask, s.uv);
			if (mask <= 0.0f) { attempts++; continue; }
			if (dist01(rng) > mask) { attempts++; continue; }
			if (!canPlace(s.pos)) { attempts++; continue; }
			roots.push_back(s);
			addToGrid((int)roots.size() - 1);
			attempts++;
		}
	} else {
		// Uniform distribution: build an even quad grid in UV space and map to surface.
		if (!meshHasUvs(mesh)) {
			// Fallback: use area CDF random (best-effort if no UVs).
			int attempts = 0;
			while ((int)roots.size() < targetCount && attempts < maxAttempts) {
				HairRootSample s = sampleRandomRoot();
				float mask = sampleMaskValue(m_distMask, s.uv);
				if (mask <= 0.0f) { attempts++; continue; }
				if (dist01(rng) > mask) { attempts++; continue; }
				roots.push_back(s);
				attempts++;
			}
		} else {
			const int grid = (int)std::ceil(std::sqrt((float)targetCount));
			if (grid <= 0) return;

			// Build a coarse UV-space grid for triangle lookup
			const int uvGrid = glm::clamp((int)std::sqrt((float)triCount), 16, 256);
			std::vector<std::vector<int>> uvCells((size_t)uvGrid * (size_t)uvGrid);
			auto toCell = [&](const glm::vec2& uv) -> glm::ivec2 {
				float u = uv.x - std::floor(uv.x);
				float v = uv.y - std::floor(uv.y);
				int x = (int)glm::clamp(std::floor(u * uvGrid), 0.0f, (float)(uvGrid - 1));
				int y = (int)glm::clamp(std::floor(v * uvGrid), 0.0f, (float)(uvGrid - 1));
				return glm::ivec2(x, y);
			};

			for (size_t ti = 0; ti < triCount; ti++) {
				unsigned int i0 = indices[ti * 3u + 0u];
				unsigned int i1 = indices[ti * 3u + 1u];
				unsigned int i2 = indices[ti * 3u + 2u];
				glm::vec2 uv0 = mesh.uvs()[i0];
				glm::vec2 uv1 = mesh.uvs()[i1];
				glm::vec2 uv2 = mesh.uvs()[i2];
				glm::vec2 mn = glm::min(uv0, glm::min(uv1, uv2));
				glm::vec2 mx = glm::max(uv0, glm::max(uv1, uv2));
				// Wrap to [0,1)
				mn = glm::vec2(mn.x - std::floor(mn.x), mn.y - std::floor(mn.y));
				mx = glm::vec2(mx.x - std::floor(mx.x), mx.y - std::floor(mx.y));
				glm::ivec2 cmin = toCell(mn);
				glm::ivec2 cmax = toCell(mx);
				for (int y = cmin.y; y <= cmax.y; y++) {
					for (int x = cmin.x; x <= cmax.x; x++) {
						uvCells[(size_t)y * (size_t)uvGrid + (size_t)x].push_back((int)ti);
					}
				}
			}

			int produced = 0;
			for (int gy = 0; gy < grid && produced < targetCount; gy++) {
				for (int gx = 0; gx < grid && produced < targetCount; gx++) {
					int idx = gy * grid + gx;
					if (idx >= targetCount) break;
					glm::vec2 uv;
					uv.x = ((float)gx + 0.5f) / (float)grid;
					uv.y = ((float)gy + 0.5f) / (float)grid;
					glm::ivec2 c = toCell(uv);
					const auto& cell = uvCells[(size_t)c.y * (size_t)uvGrid + (size_t)c.x];
					bool found = false;
					for (int tri : cell) {
						unsigned int i0 = indices[(size_t)tri * 3u + 0u];
						unsigned int i1 = indices[(size_t)tri * 3u + 1u];
						unsigned int i2 = indices[(size_t)tri * 3u + 2u];
						glm::vec2 uv0 = mesh.uvs()[i0];
						glm::vec2 uv1 = mesh.uvs()[i1];
						glm::vec2 uv2 = mesh.uvs()[i2];
						glm::vec3 bary;
						if (!barycentric2D(uv, uv0, uv1, uv2, bary)) continue;
						HairRootSample s = sampleTriangleBary(mesh, tri, bary);
						float mask = sampleMaskValue(m_distMask, s.uv);
						if (mask <= 0.0f) { found = true; break; }
						uint32_t h = (uint32_t)idx * 2654435761u;
						h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
						float mv = (float)(h & 0x00FFFFFFu) / (float)0x01000000u;
						if (mv > mask) { found = true; break; }
						roots.push_back(s);
						produced++;
						found = true;
						break;
					}
					if (!found) {
						// No triangle in this UV cell; skip.
						continue;
					}
				}
			}
		}
	}

	int uniqueRootCount = (int)roots.size();
	// If masks are too restrictive, duplicate existing samples to reach target count.
	if (!roots.empty() && (int)roots.size() < targetCount) {
		std::uniform_int_distribution<int> pick(0, (int)roots.size() - 1);
		while ((int)roots.size() < targetCount) {
			roots.push_back(roots[(size_t)pick(rng)]);
		}
	}

	if (roots.empty()) return;

	// Build guide lookup
	std::vector<const HairCurve*> guideCurves;
	std::vector<glm::vec3> guideRoots;
	std::vector<float> guideLengths;
	guideCurves.reserve(m_guides.curveCount());
	guideRoots.reserve(m_guides.curveCount());
	guideLengths.reserve(m_guides.curveCount());
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		const HairCurve& c = m_guides.curve(ci);
		if (!c.visible) continue;
		if (c.points.size() < 2) continue;
		float len = 0.0f;
		for (size_t i = 0; i + 1 < c.points.size(); i++) len += glm::length(c.points[i + 1] - c.points[i]);
		if (len <= 1e-6f) continue;
		guideCurves.push_back(&c);
		guideRoots.push_back(c.points[0]);
		guideLengths.push_back(len);
	}

	const int steps = glm::max(2, m_guideSettings.defaultSteps);
	const float defaultLen = m_guideSettings.defaultLength;

	int strandCount = 0;
	int rootIndex = 0;
	for (const auto& r : roots) {
		float lenMask = sampleMaskValue(m_lenMask, r.uv);
		float maxLen = defaultLen;
		const HairCurve* guide = nullptr;
		glm::vec3 guideRoot(0.0f);
		float guideLen = 0.0f;
		if (!guideCurves.empty()) {
			float best = std::numeric_limits<float>::max();
			for (size_t gi = 0; gi < guideCurves.size(); gi++) {
				float d = glm::length(r.pos - guideRoots[gi]);
				if (d < best) {
					best = d;
					guide = guideCurves[gi];
					guideRoot = guideRoots[gi];
					guideLen = guideLengths[gi];
				}
			}
			if (guide) maxLen = guideLen;
		}

		float hairLen = glm::clamp(lenMask, 0.0f, 1.0f) * maxLen;
		if (hairLen <= 1e-5f) continue;

		std::vector<glm::vec3> pts;
		pts.reserve((size_t)steps);
		if (guide) {
			float useLen = glm::min(hairLen, guideLen);
			glm::vec3 delta = r.pos - guideRoot;
			for (int si = 0; si < steps; si++) {
				float t = (steps <= 1) ? 0.0f : (float)si / (float)(steps - 1);
				float s = t * useLen;
				glm::vec3 gp = sampleCurveAtLength(*guide, s);
				pts.push_back(gp + delta);
			}
		} else {
			for (int si = 0; si < steps; si++) {
				float t = (steps <= 1) ? 0.0f : (float)si / (float)(steps - 1);
				pts.push_back(r.pos + r.nrm * (hairLen * t));
			}
		}

		size_t indexStart = out.indices.size();
		for (int si = 0; si + 1 < steps; si++) {
			glm::vec3 p0 = pts[(size_t)si];
			glm::vec3 p1 = pts[(size_t)si + 1u];
			glm::vec3 tan = p1 - p0;
			float tanLen = glm::length(tan);
			if (tanLen <= 1e-6f) continue;
			tan /= tanLen;

			float t0 = (steps <= 1) ? 0.0f : (float)si / (float)(steps - 1);
			float t1 = (steps <= 1) ? 1.0f : (float)(si + 1) / (float)(steps - 1);
			float s0 = t0 * hairLen;
			float s1 = t1 * hairLen;

			uint32_t base = (uint32_t)(out.vertices.size() / 9u);
			auto pushVert = [&](const glm::vec3& p, const glm::vec3& t, float s, float side, float len) {
				out.vertices.push_back(p.x);
				out.vertices.push_back(p.y);
				out.vertices.push_back(p.z);
				out.vertices.push_back(t.x);
				out.vertices.push_back(t.y);
				out.vertices.push_back(t.z);
				out.vertices.push_back(s);
				out.vertices.push_back(side);
				out.vertices.push_back(len);
			};

			pushVert(p0, tan, s0, -1.0f, hairLen);
			pushVert(p0, tan, s0,  1.0f, hairLen);
			pushVert(p1, tan, s1, -1.0f, hairLen);
			pushVert(p1, tan, s1,  1.0f, hairLen);

			out.indices.push_back(base + 0u);
			out.indices.push_back(base + 1u);
			out.indices.push_back(base + 2u);
			out.indices.push_back(base + 2u);
			out.indices.push_back(base + 1u);
			out.indices.push_back(base + 3u);
		}
		if (out.indices.size() > indexStart && rootIndex < uniqueRootCount) {
			strandCount++;
		}
		rootIndex++;
	}

	m_lastHairCount = strandCount;
}

void Scene::buildHairStrands(HairStrandData& out) const {
	out.points.clear();
	out.lengths.clear();
	out.strandCount = 0;
	out.steps = 0;
	m_lastHairCount = 0;

	if (!m_mesh) return;
	if (m_hairSettings.hairCount <= 0) return;

	const Mesh& mesh = *m_mesh;
	const auto& positions = mesh.positions();
	const auto& indices = mesh.indices();
	if (positions.empty() || indices.size() < 3) return;
	const size_t triCount = indices.size() / 3u;
	if (triCount == 0) return;

	std::mt19937 rng(1337u);
	std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

	// Build triangle area CDF for sampling
	std::vector<float> areas(triCount, 0.0f);
	float totalArea = 0.0f;
	for (size_t ti = 0; ti < triCount; ti++) {
		unsigned int i0 = indices[ti * 3u + 0u];
		unsigned int i1 = indices[ti * 3u + 1u];
		unsigned int i2 = indices[ti * 3u + 2u];
		float a = triArea(positions[i0], positions[i1], positions[i2]);
		areas[ti] = a;
		totalArea += a;
	}
	if (totalArea <= 1e-8f) return;
	std::vector<float> cdf(triCount, 0.0f);
	float accum = 0.0f;
	for (size_t i = 0; i < triCount; i++) {
		accum += areas[i] / totalArea;
		cdf[i] = accum;
	}

	auto pickTriangle = [&](float r) -> int {
		auto it = std::lower_bound(cdf.begin(), cdf.end(), r);
		if (it == cdf.end()) return (int)(cdf.size() - 1);
		return (int)std::distance(cdf.begin(), it);
	};

	auto sampleRandomRoot = [&]() -> HairRootSample {
		float r = dist01(rng);
		int tri = pickTriangle(r);
		float r1 = dist01(rng);
		float r2 = dist01(rng);
		return sampleTriangle(mesh, tri, r1, r2);
	};

	std::vector<HairRootSample> roots;
	roots.reserve((size_t)m_hairSettings.hairCount);

	int targetCount = m_hairSettings.hairCount;
	const int maxAttempts = glm::max(targetCount * 200, 5000);
	const float spacing = glm::max(std::sqrt(totalArea / (float)glm::max(targetCount, 1)), 1e-5f);

	if (m_hairSettings.distribution == HairDistributionType::Vertex) {
		const size_t vcount = positions.size();
		if (vcount == 0) return;
		targetCount = (int)vcount;
		int attempts = 0;
		while ((int)roots.size() < targetCount && attempts < maxAttempts) {
			size_t vi = (size_t)(dist01(rng) * (float)vcount) % vcount;
			HairRootSample s;
			s.pos = positions[vi];
			if (meshHasNormals(mesh)) s.nrm = glm::normalize(mesh.normals()[vi]);
			if (meshHasUvs(mesh)) s.uv = mesh.uvs()[vi];
			float mask = sampleMaskValue(m_distMask, s.uv);
			if (mask <= 0.0f) { attempts++; continue; }
			if (dist01(rng) > mask) { attempts++; continue; }
			roots.push_back(s);
			attempts++;
		}
	} else if (m_hairSettings.distribution == HairDistributionType::Even) {
		const float cellSize = spacing;
		if (cellSize <= 1e-6f) return;
		struct CellKey { int x = 0, y = 0, z = 0; };
		auto hashCell = [](int x, int y, int z) -> uint64_t {
			uint64_t h = 1469598103934665603ull;
			h ^= (uint64_t)(x * 73856093); h *= 1099511628211ull;
			h ^= (uint64_t)(y * 19349663); h *= 1099511628211ull;
			h ^= (uint64_t)(z * 83492791); h *= 1099511628211ull;
			return h;
		};
		std::unordered_map<uint64_t, std::vector<int>> grid;
		auto cellOf = [&](const glm::vec3& p) -> CellKey {
			return CellKey{ (int)std::floor(p.x / cellSize), (int)std::floor(p.y / cellSize), (int)std::floor(p.z / cellSize) };
		};
		auto canPlace = [&](const glm::vec3& p) -> bool {
			CellKey c = cellOf(p);
			for (int dz = -1; dz <= 1; dz++) {
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						uint64_t h = hashCell(c.x + dx, c.y + dy, c.z + dz);
						auto it = grid.find(h);
						if (it == grid.end()) continue;
						for (int idx : it->second) {
							float d = glm::length(p - roots[(size_t)idx].pos);
							if (d < spacing) return false;
						}
					}
				}
			}
			return true;
		};
		auto addToGrid = [&](int idx) {
			CellKey c = cellOf(roots[(size_t)idx].pos);
			uint64_t h = hashCell(c.x, c.y, c.z);
			grid[h].push_back(idx);
		};

		int attempts = 0;
		int maxEvenAttempts = glm::max(targetCount * 20, 2000);
		while ((int)roots.size() < targetCount && attempts < maxEvenAttempts) {
			HairRootSample s = sampleRandomRoot();
			float mask = sampleMaskValue(m_distMask, s.uv);
			if (mask <= 0.0f) { attempts++; continue; }
			if (dist01(rng) > mask) { attempts++; continue; }
			if (!canPlace(s.pos)) { attempts++; continue; }
			roots.push_back(s);
			addToGrid((int)roots.size() - 1);
			attempts++;
		}
	} else {
		// Uniform distribution: build an even quad grid in UV space and map to surface.
		if (!meshHasUvs(mesh)) {
			int attempts = 0;
			while ((int)roots.size() < targetCount && attempts < maxAttempts) {
				HairRootSample s = sampleRandomRoot();
				float mask = sampleMaskValue(m_distMask, s.uv);
				if (mask <= 0.0f) { attempts++; continue; }
				if (dist01(rng) > mask) { attempts++; continue; }
				roots.push_back(s);
				attempts++;
			}
		} else {
			const int grid = (int)std::ceil(std::sqrt((float)targetCount));
			if (grid <= 0) return;
			const int uvGrid = glm::clamp((int)std::sqrt((float)triCount), 16, 256);
			std::vector<std::vector<int>> uvCells((size_t)uvGrid * (size_t)uvGrid);
			auto toCell = [&](const glm::vec2& uv) -> glm::ivec2 {
				float u = uv.x - std::floor(uv.x);
				float v = uv.y - std::floor(uv.y);
				int x = (int)glm::clamp(std::floor(u * uvGrid), 0.0f, (float)(uvGrid - 1));
				int y = (int)glm::clamp(std::floor(v * uvGrid), 0.0f, (float)(uvGrid - 1));
				return glm::ivec2(x, y);
			};

			for (size_t ti = 0; ti < triCount; ti++) {
				unsigned int i0 = indices[ti * 3u + 0u];
				unsigned int i1 = indices[ti * 3u + 1u];
				unsigned int i2 = indices[ti * 3u + 2u];
				glm::vec2 uv0 = mesh.uvs()[i0];
				glm::vec2 uv1 = mesh.uvs()[i1];
				glm::vec2 uv2 = mesh.uvs()[i2];
				glm::vec2 mn = glm::min(uv0, glm::min(uv1, uv2));
				glm::vec2 mx = glm::max(uv0, glm::max(uv1, uv2));
				mn = glm::vec2(mn.x - std::floor(mn.x), mn.y - std::floor(mn.y));
				mx = glm::vec2(mx.x - std::floor(mx.x), mx.y - std::floor(mx.y));
				glm::ivec2 cmin = toCell(mn);
				glm::ivec2 cmax = toCell(mx);
				for (int y = cmin.y; y <= cmax.y; y++) {
					for (int x = cmin.x; x <= cmax.x; x++) {
						uvCells[(size_t)y * (size_t)uvGrid + (size_t)x].push_back((int)ti);
					}
				}
			}

			int produced = 0;
			for (int gy = 0; gy < grid && produced < targetCount; gy++) {
				for (int gx = 0; gx < grid && produced < targetCount; gx++) {
					int idx = gy * grid + gx;
					if (idx >= targetCount) break;
					glm::vec2 uv;
					uv.x = ((float)gx + 0.5f) / (float)grid;
					uv.y = ((float)gy + 0.5f) / (float)grid;
					glm::ivec2 c = toCell(uv);
					const auto& cell = uvCells[(size_t)c.y * (size_t)uvGrid + (size_t)c.x];
					bool found = false;
					for (int tri : cell) {
						unsigned int i0 = indices[(size_t)tri * 3u + 0u];
						unsigned int i1 = indices[(size_t)tri * 3u + 1u];
						unsigned int i2 = indices[(size_t)tri * 3u + 2u];
						glm::vec2 uv0 = mesh.uvs()[i0];
						glm::vec2 uv1 = mesh.uvs()[i1];
						glm::vec2 uv2 = mesh.uvs()[i2];
						glm::vec3 bary;
						if (!barycentric2D(uv, uv0, uv1, uv2, bary)) continue;
						HairRootSample s = sampleTriangleBary(mesh, tri, bary);
						float mask = sampleMaskValue(m_distMask, s.uv);
						if (mask <= 0.0f) { found = true; break; }
						uint32_t h = (uint32_t)idx * 2654435761u;
						h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
						float mv = (float)(h & 0x00FFFFFFu) / (float)0x01000000u;
						if (mv > mask) { found = true; break; }
						roots.push_back(s);
						produced++;
						found = true;
						break;
					}
					if (!found) {
						continue;
					}
				}
			}
		}
	}

	int uniqueRootCount = (int)roots.size();
	// If masks are too restrictive, duplicate existing samples to reach target count.
	if (!roots.empty() && (int)roots.size() < targetCount) {
		std::uniform_int_distribution<int> pick(0, (int)roots.size() - 1);
		while ((int)roots.size() < targetCount) {
			roots.push_back(roots[(size_t)pick(rng)]);
		}
	}

	if (roots.empty()) return;

	// Build guide lookup
	std::vector<const HairCurve*> guideCurves;
	std::vector<glm::vec3> guideRoots;
	std::vector<float> guideLengths;
	guideCurves.reserve(m_guides.curveCount());
	guideRoots.reserve(m_guides.curveCount());
	guideLengths.reserve(m_guides.curveCount());
	for (size_t ci = 0; ci < m_guides.curveCount(); ci++) {
		const HairCurve& c = m_guides.curve(ci);
		if (!c.visible) continue;
		if (c.points.size() < 2) continue;
		float len = 0.0f;
		for (size_t i = 0; i + 1 < c.points.size(); i++) len += glm::length(c.points[i + 1] - c.points[i]);
		if (len <= 1e-6f) continue;
		guideCurves.push_back(&c);
		guideRoots.push_back(c.points[0]);
		guideLengths.push_back(len);
	}

	const int steps = glm::max(2, m_guideSettings.defaultSteps);
	const float defaultLen = m_guideSettings.defaultLength;

	int strandCount = 0;
	out.steps = steps;
	out.points.reserve((size_t)roots.size() * (size_t)steps * 3u);
	out.lengths.reserve((size_t)roots.size());

	int rootIndex = 0;
	for (const auto& r : roots) {
		float lenMask = sampleMaskValue(m_lenMask, r.uv);
		float maxLen = defaultLen;
		const HairCurve* guide = nullptr;
		glm::vec3 guideRoot(0.0f);
		float guideLen = 0.0f;
		if (!guideCurves.empty()) {
			float best = std::numeric_limits<float>::max();
			for (size_t gi = 0; gi < guideCurves.size(); gi++) {
				float d = glm::length(r.pos - guideRoots[gi]);
				if (d < best) {
					best = d;
					guide = guideCurves[gi];
					guideRoot = guideRoots[gi];
					guideLen = guideLengths[gi];
				}
			}
			if (guide) maxLen = guideLen;
		}

		float hairLen = glm::clamp(lenMask, 0.0f, 1.0f) * maxLen;
		if (hairLen <= 1e-5f) {
			rootIndex++;
			continue;
		}

		if (rootIndex < uniqueRootCount) {
			strandCount++;
		}
		rootIndex++;

		std::vector<glm::vec3> pts;
		pts.reserve((size_t)steps);
		if (guide) {
			float useLen = glm::min(hairLen, guideLen);
			glm::vec3 delta = r.pos - guideRoot;
			for (int si = 0; si < steps; si++) {
				float t = (steps <= 1) ? 0.0f : (float)si / (float)(steps - 1);
				float s = t * useLen;
				glm::vec3 gp = sampleCurveAtLength(*guide, s);
				pts.push_back(gp + delta);
			}
		} else {
			for (int si = 0; si < steps; si++) {
				float t = (steps <= 1) ? 0.0f : (float)si / (float)(steps - 1);
				pts.push_back(r.pos + r.nrm * (hairLen * t));
			}
		}

		for (const auto& p : pts) {
			out.points.push_back(p.x);
			out.points.push_back(p.y);
			out.points.push_back(p.z);
		}
		out.lengths.push_back(hairLen);
	}

	out.strandCount = (int)out.lengths.size();
	m_lastHairCount = strandCount;
}
