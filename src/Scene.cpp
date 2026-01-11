#include "Scene.h"

#include "Mesh.h"
#include "Raycast.h"
#include "Physics.h"
#include "GpuSolver.h"
#include "MayaCameraController.h"

#include <imgui.h>

#include <algorithm>

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

void Scene::tick() {
	// placeholder for per-frame updates
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
			if (m_guides.pickCurve(ro, rd, hc)) {
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

		int newIdx = m_guides.addCurveOnMesh(*m_mesh, hit.triIndex, hit.bary, hit.position, hit.normal, m_guideSettings);
		if (newIdx >= 0) {
			// Mirror mode: only affects newly created curves while it is enabled.
			const bool mirrorOn = m_guideSettings.mirrorMode;
			if (mirrorOn && glm::abs(hit.position.x) > 1e-5f) {
				RayHit mh;
				glm::vec3 mp = mirrorX(hit.position);
				if (Raycast::nearestOnMesh(*m_mesh, mp, mh)) {
					int mirrorIdx = m_guides.addCurveOnMesh(*m_mesh, mh.triIndex, mh.bary, mh.position, mh.normal, m_guideSettings);
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
	if (m_guides.pickControlPoint(ro, rd, camera.position(), camera.viewProj(), m_dragCurve, m_dragVert, true)) {
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
