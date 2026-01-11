#include "Physics.h"

#include "Scene.h"
#include "Mesh.h"

#include "Bvh.h"

#include <vector>

static bool rayTriMT(const glm::vec3& ro, const glm::vec3& rd,
	const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
	float& tOut) {
	glm::vec3 e1 = b - a;
	glm::vec3 e2 = c - a;
	glm::vec3 p = glm::cross(rd, e2);
	float det = glm::dot(e1, p);
	if (glm::abs(det) < 1e-8f) return false;
	float invDet = 1.0f / det;
	glm::vec3 s = ro - a;
	float u = glm::dot(s, p) * invDet;
	if (u < 0.0f || u > 1.0f) return false;
	glm::vec3 q = glm::cross(s, e1);
	float v = glm::dot(rd, q) * invDet;
	if (v < 0.0f || u + v > 1.0f) return false;
	float t = glm::dot(e2, q) * invDet;
	if (t <= 1e-6f) return false;
	tOut = t;
	return true;
}

static bool isInsideMeshRayParity(const Mesh& mesh, const Bvh& bvh, const glm::vec3& p) {
	// Odd-even rule: cast ray +X and count intersections.
	// This is approximate but works well for closed head meshes.
	const glm::vec3 ro = p + glm::vec3(1e-5f, 0, 0);
	const glm::vec3 rd(1.0f, 0.0f, 0.0f);

	const auto& pos = mesh.positions();
	const auto& ind = mesh.indices();
	int count = 0;

	bvh.raycast(ro, rd, [&](int triIndex) {
		unsigned int i0 = ind[(size_t)triIndex * 3 + 0];
		unsigned int i1 = ind[(size_t)triIndex * 3 + 1];
		unsigned int i2 = ind[(size_t)triIndex * 3 + 2];
		float t = 0.0f;
		if (rayTriMT(ro, rd, pos[i0], pos[i1], pos[i2], t)) {
			count++;
		}
	});

	return (count % 2) == 1;
}

#include <glm/glm.hpp>

static void solveDistance(glm::vec3& p0, glm::vec3& p1, float restLen, float w0, float w1, float stiffness) {
	glm::vec3 d = p1 - p0;
	float len = glm::length(d);
	if (len < 1e-8f) return;
	glm::vec3 n = d / len;
	float C = len - restLen;
	float wsum = w0 + w1;
	if (wsum <= 0.0f) return;

	// NOTE: We intentionally do NOT clamp corrections here.
	// For hair guides, stretch must be eliminated (inextensible constraints).
	// Stability is handled via substeps + damping + collision velocity zeroing.
	glm::vec3 corr = (C / wsum) * n;
	corr *= glm::clamp(stiffness, 0.0f, 1.0f);
	// Standard PBD: move points to reduce constraint error.
	// p0 moves along +n when stretched (C>0), p1 moves along -n.
	p0 += corr * ( w0);
	p1 += corr * (-w1);
}

static void integrateVerlet(std::vector<glm::vec3>& p, std::vector<glm::vec3>& prev, float dt, const glm::vec3& acc, int pinnedIndex, float damping) {
	float dt2 = dt * dt;
	for (size_t i = 0; i < p.size(); i++) {
		if ((int)i == pinnedIndex) continue;
		glm::vec3 x = p[i];
		glm::vec3 v = (x - prev[i]) * damping; // Apply damping DURING integration
		prev[i] = x;
		p[i] = x + v + acc * dt2;
	}
}

static void integrateVerlet2Pinned(std::vector<glm::vec3>& p, std::vector<glm::vec3>& prev, float dt, const glm::vec3& acc, int pinnedA, int pinnedB, float damping) {
	float dt2 = dt * dt;
	for (size_t i = 0; i < p.size(); i++) {
		if ((int)i == pinnedA || (int)i == pinnedB) continue;
		glm::vec3 x = p[i];
		glm::vec3 v = (x - prev[i]) * damping;
		prev[i] = x;
		p[i] = x + v + acc * dt2;
	}
}

void Physics::step(Scene& scene, float dt) {
	if (dt <= 0.0f) return;
	if (!scene.mesh()) return;

	static const Mesh* cachedMesh = nullptr;
	static Bvh meshBvh;
	if (cachedMesh != scene.mesh()) {
		meshBvh.build(*scene.mesh());
		cachedMesh = scene.mesh();
	}

	// DEBUG: Check state on first call
	static int callCount = 0;
	callCount++;
	
	// Update roots BEFORE integration to ensure zero initial velocity
	scene.guides().updatePinnedRootsFromMesh(*scene.mesh());

	GuideSettings& gs = scene.guideSettings();

	const bool dragging = scene.isDragging();
	const int dragCurve = scene.dragCurve();
	const int dragVert = scene.dragVert();

	for (size_t ci = 0; ci < scene.guides().curveCount(); ci++) {
		if (!scene.guides().isCurveSelected(ci)) {
			continue; // freeze unselected curves
		}

		// Mesh positions are already scaled to meters at import time, so gravity is standard m/s^2.
		float g = glm::max(0.0f, scene.effectiveGravityForCurve(ci));
		glm::vec3 gravity(0.0f, -g, 0.0f);

		HairCurve& c = scene.guides().curve(ci);
		if (c.points.size() < 2) continue;

		// HTML spiderweb invariant: prevPos starts equal to pos.
		// Enforce the same here defensively (prevents garbage velocities).
		if (c.prevPoints.size() != c.points.size()) {
			c.prevPoints = c.points;
		}

		// Kill obviously corrupted velocities (prevents instant drift to infinity).
		// Threshold is in m/s (world units). With dt~=0.001, 50 m/s => 5cm per substep.
		const float maxReasonableSpeed = 50.0f;
		const float maxDisp = maxReasonableSpeed * dt;
		for (size_t i = 0; i < c.points.size(); i++) {
			const glm::vec3 dp = c.points[i] - c.prevPoints[i];
			if (glm::any(glm::isnan(dp)) || glm::any(glm::isinf(dp)) || glm::length(dp) > maxDisp) {
				c.prevPoints[i] = c.points[i];
			}
		}

		// Diagnostic: check for NaN or inf positions before integration
		bool hasInvalid = false;
		size_t invalidIdx = 0;
		for (size_t i = 0; i < c.points.size(); i++) {
			if (glm::any(glm::isnan(c.points[i])) || glm::any(glm::isinf(c.points[i]))) {
				hasInvalid = true;
				invalidIdx = i;
				printf("ERROR: Curve %zu vertex %zu has NaN/inf: (%.3f, %.3f, %.3f)\n", 
				       ci, i, c.points[i].x, c.points[i].y, c.points[i].z);
				break;
			}
		}
		if (hasInvalid) {
			// Reset this curve to prevent further corruption
			printf("Removing corrupted curve %zu\n", ci);
			scene.guides().removeCurve((int)ci);
			continue;
		}

		// Verlet integration with damping (like spiderweb)
		const float dampingFactor = glm::clamp(gs.damping, 0.0f, 1.0f);
		const int pinnedRoot = 0;
		const int pinnedDrag = (dragging && (int)ci == dragCurve) ? dragVert : -1;
		if (pinnedDrag >= 0) {
			// While dragging, pin the dragged vertex so constraints can't fight the mouse.
			c.prevPoints[(size_t)pinnedDrag] = c.points[(size_t)pinnedDrag];
			integrateVerlet2Pinned(c.points, c.prevPoints, dt, gravity, pinnedRoot, pinnedDrag, dampingFactor);
		} else {
			integrateVerlet(c.points, c.prevPoints, dt, gravity, pinnedRoot, dampingFactor);
		}

		// Constraints
		int iters = glm::clamp(gs.solverIterations, 1, 64);
		float rest = c.segmentRestLen;
		if (rest <= 0.0f) {
			rest = gs.defaultLength / (float)(glm::max(2, (int)c.points.size()) - 1);
			c.segmentRestLen = rest;
		}

		float bendStiffness = glm::clamp(gs.stiffness, 0.0f, 1.0f);
		for (int it = 0; it < iters; it++) {
			// distance constraints
			for (size_t i = 0; i + 1 < c.points.size(); i++) {
				float w0 = (i == 0) ? 0.0f : 1.0f;
				float w1 = 1.0f;
				if (pinnedDrag >= 0) {
					if ((int)i == pinnedDrag) w0 = 0.0f;
					if ((int)(i + 1) == pinnedDrag) w1 = 0.0f;
					if (w0 + w1 <= 0.0f) continue;
				}
				// Stretch constraints are always full-strength (hair is inextensible)
				solveDistance(c.points[i], c.points[i + 1], rest, w0, w1, 1.0f);
			}

			// Bend stiffness (second-neighbor distance). This resists sharp kinks without allowing stretch.
			if (bendStiffness > 0.0f) {
				for (size_t i = 0; i + 2 < c.points.size(); i++) {
					float w0 = (i == 0) ? 0.0f : 1.0f;
					float w2 = 1.0f;
					if (pinnedDrag >= 0) {
						if ((int)i == pinnedDrag) w0 = 0.0f;
						if ((int)(i + 2) == pinnedDrag) w2 = 0.0f;
						if (w0 + w2 <= 0.0f) continue;
					}
					solveDistance(c.points[i], c.points[i + 2], rest * 2.0f, w0, w2, bendStiffness);
				}
			}

			// OBJ mesh collision: nearest-triangle pushout with thickness.
			if (gs.enableMeshCollision) {
				float thickness = glm::max(1e-6f, gs.collisionThickness);
				for (size_t i = 1; i < c.points.size(); i++) {
					int tri = -1;
					glm::vec3 cp, n;
					if (!meshBvh.nearestTriangle(c.points[i], tri, cp, n, thickness * 2.0f)) continue;
					glm::vec3 d = c.points[i] - cp;
					float dist = glm::length(d);
					if (dist < thickness) {
						bool inside = isInsideMeshRayParity(*scene.mesh(), meshBvh, c.points[i]);
						glm::vec3 pushDir;
						if (dist >= 1e-8f) {
							pushDir = inside ? -glm::normalize(d) : glm::normalize(d);
						} else {
							// Degenerate: push along triangle normal, try to push outward by checking which direction increases distance.
							pushDir = inside ? n : n;
						}
						c.points[i] += pushDir * (thickness - dist);
						// Collision response: remove normal velocity and apply friction to tangential velocity.
						// friction=0 => keep tangential velocity (slide), friction=1 => fully sticky.
						glm::vec3 nrm = pushDir;
						float nl = glm::length(nrm);
						if (nl > 1e-8f) nrm /= nl;
						glm::vec3 v = c.points[i] - c.prevPoints[i];
						glm::vec3 vN = glm::dot(v, nrm) * nrm;
						glm::vec3 vT = v - vN;
						float fr = glm::clamp(gs.collisionFriction, 0.0f, 1.0f);
						glm::vec3 vNew = vT * (1.0f - fr);
						c.prevPoints[i] = c.points[i] - vNew;
					}
				}
			}
		}

		// Diagnostic: check velocities and positions after all constraints
		float maxVel = 0.0f;
		float maxDist = 0.0f;
		for (size_t i = 0; i < c.points.size(); i++) {
			glm::vec3 v = c.points[i] - c.prevPoints[i];
			float velMag = glm::length(v) / dt;  // velocity in m/s
			maxVel = glm::max(maxVel, velMag);
			float dist = glm::length(c.points[i]);
			maxDist = glm::max(maxDist, dist);
		}
		// Warn if curves are moving too fast or too far from origin
		if (maxVel > 10.0f || maxDist > 5.0f) {  // 10 m/s or 5m from origin seems extreme
			// Only print every 60 frames to reduce spam
			static int warnCounter = 0;
			if (warnCounter++ % 60 == 0) {
				printf("WARNING: Curve %zu - maxVel=%.2f m/s, maxDist=%.2f m (may disappear soon)\n", ci, maxVel, maxDist);
			}
		}
	}

	applyCurveCurveCollision(scene);
}

void Physics::applyCurveCurveCollision(Scene& scene) {
	GuideSettings& gs = scene.guideSettings();
	if (!gs.enableCurveCollision) return;
	if (scene.guides().curveCount() < 2) return;

	float r = glm::max(1e-5f, gs.collisionThickness);
	float r2 = r * r;
	for (size_t a = 0; a < scene.guides().curveCount(); a++) {
		if (!scene.guides().isCurveSelected(a)) continue;
		for (size_t b = a + 1; b < scene.guides().curveCount(); b++) {
			if (!scene.guides().isCurveSelected(b)) continue;
			HairCurve& ca = scene.guides().curve(a);
			HairCurve& cb = scene.guides().curve(b);
			for (size_t ia = 1; ia < ca.points.size(); ia++) {
				for (size_t ib = 1; ib < cb.points.size(); ib++) {
					glm::vec3 d = cb.points[ib] - ca.points[ia];
					float d2 = glm::dot(d, d);
					if (d2 < 1e-12f || d2 > r2) continue;
					float dist = glm::sqrt(d2);
					glm::vec3 n = d / dist;
					float pen = (r - dist);
					ca.points[ia] -= n * (0.5f * pen);
					cb.points[ib] += n * (0.5f * pen);
				}
			}
		}
	}
}
