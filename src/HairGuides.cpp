#include "HairGuides.h"

#include "Mesh.h"
#include "Log.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <limits>

void HairGuideSet::clear() {
	m_curves.clear();
	m_selected.clear();
	m_activeCurve = -1;
}

int HairGuideSet::addCurveOnMesh(const Mesh& mesh, int triIndex, const glm::vec3& bary, const glm::vec3& hitPos, const glm::vec3& hitNormal, const GuideSettings& settings) {
	HairCurve c;
	// Validate and store binding.
	// If the triangle index is invalid, we still allow spawning (unpinned root),
	// but we must not use it during physics.
	{
		const auto& ind = mesh.indices();
		const size_t triCount = ind.size() / 3;
		if (triIndex >= 0 && (size_t)triIndex < triCount) {
			c.root.triIndex = triIndex;
			glm::vec3 b = bary;
			if (glm::any(glm::isnan(b)) || glm::any(glm::isinf(b))) {
				b = glm::vec3(1.0f, 0.0f, 0.0f);
			}
			// Clamp and renormalize barycentrics to avoid drift.
			b = glm::clamp(b, glm::vec3(0.0f), glm::vec3(1.0f));
			float s = b.x + b.y + b.z;
			if (s <= 1e-8f) {
				b = glm::vec3(1.0f, 0.0f, 0.0f);
			} else {
				b /= s;
			}
			c.root.bary = b;
		} else {
			c.root.triIndex = -1;
			c.root.bary = glm::vec3(1.0f, 0.0f, 0.0f);
			HT_WARN("WARNING: addCurveOnMesh received invalid triIndex=%d (mesh tris=%zu). Root will be unpinned.\n", triIndex, triCount);
		}
	}

	int steps = glm::clamp(settings.defaultSteps, 2, 256);
	c.points.resize((size_t)steps);
	c.prevPoints.resize((size_t)steps);

	// Validate input
	
	if (glm::any(glm::isnan(hitPos)) || glm::any(glm::isinf(hitPos)) ||
	    glm::any(glm::isnan(hitNormal)) || glm::any(glm::isinf(hitNormal))) {
		HT_ERR("ERROR: Invalid hitPos or hitNormal in addCurveOnMesh\n");
		return -1;
	}

	float normalLen = glm::length(hitNormal);
	glm::vec3 dir;
	if (normalLen < 1e-6f) {
		HT_ERR("ERROR: hitNormal is zero or near-zero (len=%.6f), using default direction\n", normalLen);
		dir = glm::vec3(0.0f, 1.0f, 0.0f);
	} else {
		dir = hitNormal / normalLen;
		// Check if normalization created NaN
		if (glm::any(glm::isnan(dir)) || glm::any(glm::isinf(dir))) {
			HT_ERR("ERROR: Normalization created NaN/inf, using default direction\n");
			dir = glm::vec3(0.0f, 1.0f, 0.0f);
		}
	}
	
	float len = glm::max(0.001f, settings.defaultLength);
	c.segmentRestLen = len / (float)(steps - 1);

	// (Debug logging removed)
	
	// Compute actual mesh root position to check for mismatch with hitPos
	glm::vec3 meshRootPos = hitPos;
	if (triIndex >= 0 && (size_t)triIndex < mesh.indices().size() / 3) {
		const auto& pos = mesh.positions();
		const auto& ind = mesh.indices();
		unsigned int i0 = ind[(size_t)triIndex * 3 + 0];
		unsigned int i1 = ind[(size_t)triIndex * 3 + 1];
		unsigned int i2 = ind[(size_t)triIndex * 3 + 2];
		if (i0 < pos.size() && i1 < pos.size() && i2 < pos.size()) {
			meshRootPos = pos[i0] * c.root.bary.x + pos[i1] * c.root.bary.y + pos[i2] * c.root.bary.z;
		}
	}

	for (int i = 0; i < steps; i++) {
		float t = (float)i / (float)(steps - 1);
		glm::vec3 p = meshRootPos + dir * (len * t);
		c.points[(size_t)i] = p;
		c.prevPoints[(size_t)i] = p;
		
		// (debug logging removed)
	}

	// (Debug logging removed)

	m_curves.push_back(std::move(c));
	m_selected.push_back((unsigned char)0);
	return (int)m_curves.size() - 1;
}

static float pointRayDistance(const glm::vec3& p, const glm::vec3& ro, const glm::vec3& rd) {
	glm::vec3 v = p - ro;
	float t = glm::dot(v, rd);
	glm::vec3 q = ro + rd * t;
	return glm::length(p - q);
}

bool HairGuideSet::pickControlPoint(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& camPos, const glm::mat4& viewProj, int& outCurve, int& outVert, bool selectedOnly) const {
	(void)camPos;
	(void)viewProj;
	// MVP: pick closest point to ray in world space with a fixed threshold
	float best = std::numeric_limits<float>::infinity();
	int bestC = -1;
	int bestV = -1;

	const float threshold = 0.015f;
	for (size_t ci = 0; ci < m_curves.size(); ci++) {
		if (selectedOnly && !isCurveSelected(ci)) continue;
		const HairCurve& c = m_curves[ci];
		for (size_t vi = 0; vi < c.points.size(); vi++) {
			// Don't pick root (pinned)
			if (vi == 0) continue;
			float d = pointRayDistance(c.points[vi], ro, rd);
			if (d < threshold && d < best) {
				best = d;
				bestC = (int)ci;
				bestV = (int)vi;
			}
		}
	}

	if (bestC < 0) return false;
	outCurve = bestC;
	outVert = bestV;
	return true;
}

static float raySegmentDistance(const glm::vec3& ro, const glm::vec3& rdNorm, const glm::vec3& a, const glm::vec3& b) {
	glm::vec3 ab = b - a;
	float ab2 = glm::dot(ab, ab);
	if (ab2 < 1e-12f) {
		// Segment is a point.
		glm::vec3 v = a - ro;
		float s = glm::dot(v, rdNorm);
		if (s < 0.0f) s = 0.0f;
		glm::vec3 pr = ro + rdNorm * s;
		return glm::length(a - pr);
	}

	glm::vec3 ao = ro - a;
	float rdab = glm::dot(rdNorm, ab);
	float rdao = glm::dot(rdNorm, ao);
	float abao = glm::dot(ab, ao);
	float denom = ab2 - rdab * rdab;

	float t = 0.0f;
	if (glm::abs(denom) > 1e-8f) {
		// Solve for closest points between infinite line and segment, then clamp.
		t = (rdab * rdao - abao) / denom;
		t = glm::clamp(t, 0.0f, 1.0f);
	} else {
		// Nearly parallel: just project origin onto segment.
		t = glm::clamp(-abao / ab2, 0.0f, 1.0f);
	}

	glm::vec3 ps = a + ab * t;
	float s = glm::dot(ps - ro, rdNorm);
	if (s < 0.0f) s = 0.0f;
	glm::vec3 pr = ro + rdNorm * s;

	// One refinement step (project pr to segment) to improve stability.
	t = glm::clamp(glm::dot(pr - a, ab) / ab2, 0.0f, 1.0f);
	ps = a + ab * t;
	s = glm::dot(ps - ro, rdNorm);
	if (s < 0.0f) s = 0.0f;
	pr = ro + rdNorm * s;

	return glm::length(ps - pr);
}

bool HairGuideSet::pickCurve(const glm::vec3& ro, const glm::vec3& rd, int& outCurve) const {
	if (m_curves.empty()) return false;
	glm::vec3 rdNorm = rd;
	float rdl = glm::length(rdNorm);
	if (rdl < 1e-8f) return false;
	rdNorm /= rdl;

	float best = std::numeric_limits<float>::infinity();
	int bestC = -1;

	// Threshold is in world units (meters). Keep it a bit larger than point picking.
	const float threshold = 0.025f;
	for (size_t ci = 0; ci < m_curves.size(); ci++) {
		const HairCurve& c = m_curves[ci];
		if (c.points.size() < 2) continue;
		for (size_t i = 0; i + 1 < c.points.size(); i++) {
			float d = raySegmentDistance(ro, rdNorm, c.points[i], c.points[i + 1]);
			if (d < threshold && d < best) {
				best = d;
				bestC = (int)ci;
			}
		}
	}

	if (bestC < 0) return false;
	outCurve = bestC;
	return true;
}

void HairGuideSet::moveControlPoint(int curveIdx, int vertIdx, const glm::vec3& worldPos) {
	if (curveIdx < 0 || (size_t)curveIdx >= m_curves.size()) return;
	HairCurve& c = m_curves[(size_t)curveIdx];
	if (vertIdx <= 0 || (size_t)vertIdx >= c.points.size()) return;

	// Validate input position
	if (glm::any(glm::isnan(worldPos)) || glm::any(glm::isinf(worldPos))) {
		HT_ERR("ERROR: Invalid worldPos in moveControlPoint\n");
		return;
	}

	// Only move the dragged vertex and zero its velocity
	// Let the physics solver handle propagating constraints naturally
	// This prevents creating large instantaneous corrections that cause instability
	c.points[(size_t)vertIdx] = worldPos;
	c.prevPoints[(size_t)vertIdx] = worldPos;  // Zero velocity for dragged vertex
}

void HairGuideSet::removeCurve(int curveIdx) {
	if (curveIdx < 0 || (size_t)curveIdx >= m_curves.size()) return;
	m_curves.erase(m_curves.begin() + curveIdx);
	if ((size_t)curveIdx < m_selected.size()) {
		m_selected.erase(m_selected.begin() + curveIdx);
	}
	if (m_activeCurve == curveIdx) {
		m_activeCurve = -1;
		for (size_t i = 0; i < m_selected.size(); i++) {
			if (m_selected[i]) {
				m_activeCurve = (int)i;
				break;
			}
		}
	} else if (m_activeCurve > curveIdx) {
		m_activeCurve--;
	}
}

void HairGuideSet::removeCurves(const std::vector<int>& curveIndicesDescending) {
	for (int idx : curveIndicesDescending) {
		removeCurve(idx);
	}
}

bool HairGuideSet::isCurveSelected(size_t curveIdx) const {
	if (curveIdx >= m_selected.size()) return false;
	return m_selected[curveIdx] != 0;
}

void HairGuideSet::deselectAll() {
	for (size_t i = 0; i < m_selected.size(); i++) m_selected[i] = 0;
	m_activeCurve = -1;
}

void HairGuideSet::selectCurve(int curveIdx, bool additive) {
	if (curveIdx < 0 || (size_t)curveIdx >= m_curves.size()) return;
	if (!additive) {
		deselectAll();
	}
	m_selected[(size_t)curveIdx] = 1;
	m_activeCurve = curveIdx;
}

void HairGuideSet::toggleCurveSelected(int curveIdx) {
	if (curveIdx < 0 || (size_t)curveIdx >= m_curves.size()) return;
	unsigned char& s = m_selected[(size_t)curveIdx];
	s = (s ? (unsigned char)0 : (unsigned char)1);
	if (s) {
		m_activeCurve = curveIdx;
	} else if (m_activeCurve == curveIdx) {
		m_activeCurve = -1;
		for (size_t i = 0; i < m_selected.size(); i++) {
			if (m_selected[i]) {
				m_activeCurve = (int)i;
				break;
			}
		}
	}
}

std::vector<int> HairGuideSet::selectedCurves() const {
	std::vector<int> out;
	out.reserve(m_selected.size());
	for (size_t i = 0; i < m_selected.size(); i++) {
		if (m_selected[i]) out.push_back((int)i);
	}
	return out;
}

glm::vec3 HairGuideSet::evalCatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
	float t2 = t * t;
	float t3 = t2 * t;
	return 0.5f * ((2.0f * p1) +
		(-p0 + p2) * t +
		(2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
		(-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void HairGuideSet::buildCurveRenderPoints(const HairCurve& c, std::vector<glm::vec3>& outPoints) {
	outPoints.clear();
	if (c.points.size() < 2) return;

	const int samplesPerSeg = 8;
	for (size_t i = 0; i + 1 < c.points.size(); i++) {
		glm::vec3 p0 = (i == 0) ? c.points[i] : c.points[i - 1];
		glm::vec3 p1 = c.points[i];
		glm::vec3 p2 = c.points[i + 1];
		glm::vec3 p3 = (i + 2 < c.points.size()) ? c.points[i + 2] : c.points[i + 1];

		for (int s = 0; s < samplesPerSeg; s++) {
			float t = (float)s / (float)samplesPerSeg;
			outPoints.push_back(evalCatmullRom(p0, p1, p2, p3, t));
		}
	}
	outPoints.push_back(c.points.back());
}

void HairGuideSet::drawDebugLines(const glm::mat4& viewProj, unsigned int lineProgram, float pointSizePx, float deselectedOpacity, int hoverCurve, bool hoverHighlightRed) const {
	glUseProgram(lineProgram);
	glUniformMatrix4fv(glGetUniformLocation(lineProgram, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));

	unsigned int vao = 0, vbo = 0;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));

	std::vector<float> packed;
	std::vector<glm::vec3> renderPts;

	for (size_t ci = 0; ci < m_curves.size(); ci++) {
		const HairCurve& c = m_curves[ci];
		buildCurveRenderPoints(c, renderPts);

		packed.clear();
		packed.reserve(renderPts.size() * 7);
		const bool isHover = hoverHighlightRed && ((int)ci == hoverCurve);
		deselectedOpacity = glm::clamp(deselectedOpacity, 0.0f, 1.0f);
		const bool selected = isCurveSelected(ci);
		float a = isHover ? 1.0f : (selected ? 1.0f : deselectedOpacity);
		glm::vec4 baseCol(0.90f, 0.75f, 0.22f, a); // Maya-ish yellow/orange
		glm::vec4 hoverCol(1.0f, 0.15f, 0.15f, a);
		glm::vec4 col = isHover ? hoverCol : baseCol;

		for (size_t i = 0; i < renderPts.size(); i++) {
			glm::vec3 p = renderPts[i];
			packed.push_back(p.x); packed.push_back(p.y); packed.push_back(p.z);
			packed.push_back(col.r); packed.push_back(col.g); packed.push_back(col.b); packed.push_back(col.a);
		}

		glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(float), packed.data(), GL_STREAM_DRAW);
		glDrawArrays(GL_LINE_STRIP, 0, (int)renderPts.size());

		// Draw control points only for selected curves
		if (isCurveSelected(ci)) {
			packed.clear();
			packed.reserve(c.points.size() * 7);
			for (size_t vi = 0; vi < c.points.size(); vi++) {
				glm::vec3 p = c.points[vi];
				glm::vec4 pcol = (vi == 0) ? glm::vec4(0.2f, 0.9f, 0.2f, 1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
				packed.push_back(p.x); packed.push_back(p.y); packed.push_back(p.z);
				packed.push_back(pcol.r); packed.push_back(pcol.g); packed.push_back(pcol.b); packed.push_back(pcol.a);
			}
			glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(float), packed.data(), GL_STREAM_DRAW);
			glPointSize(glm::clamp(pointSizePx, 1.0f, 32.0f));
			glDrawArrays(GL_POINTS, 0, (int)c.points.size());
		}
	}

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
	glUseProgram(0);
}

void HairGuideSet::updatePinnedRootsFromMesh(const Mesh& mesh) {
	const auto& pos = mesh.positions();
	const auto& ind = mesh.indices();
	if (pos.empty() || ind.empty()) return;
	const size_t triCount = ind.size() / 3;

	for (HairCurve& c : m_curves) {
		if (c.root.triIndex < 0) continue;
		const int ti = c.root.triIndex;
		if ((size_t)ti >= triCount) {
			// Defensive: prevent out-of-bounds access which can create NaNs/Inf.
			c.root.triIndex = -1;
			HT_WARN("WARNING: Curve root had invalid triIndex=%d (mesh tris=%zu). Unpinning root.\n", ti, triCount);
			continue;
		}
		const unsigned int i0 = ind[(size_t)ti * 3 + 0];
		const unsigned int i1 = ind[(size_t)ti * 3 + 1];
		const unsigned int i2 = ind[(size_t)ti * 3 + 2];
		if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size()) {
			c.root.triIndex = -1;
			HT_WARN("WARNING: Curve root triangle indices out of range. Unpinning root.\n");
			continue;
		}
		glm::vec3 b = c.root.bary;
		if (glm::any(glm::isnan(b)) || glm::any(glm::isinf(b))) {
			b = glm::vec3(1.0f, 0.0f, 0.0f);
		}
		float s = b.x + b.y + b.z;
		if (s <= 1e-8f) b = glm::vec3(1.0f, 0.0f, 0.0f);
		else b /= s;
		glm::vec3 p = pos[i0] * b.x + pos[i1] * b.y + pos[i2] * b.z;
		
		// (Debug logging removed)

		if (glm::any(glm::isnan(p)) || glm::any(glm::isinf(p))) {
			c.root.triIndex = -1;
			HT_WARN("WARNING: Root evaluation produced NaN/Inf. Unpinning root.\n");
			continue;
		}
		if (!c.points.empty()) {
			// Store velocity before update
			glm::vec3 oldVel(0.0f);
			if (c.points.size() > 1) {
				oldVel = c.points[1] - c.prevPoints[1];
			}
			
			c.points[0] = p;
			c.prevPoints[0] = p;
			
			// DEBUG: Check if this created velocity on vertex 1
			if (c.points.size() > 1) {
				glm::vec3 newVel = c.points[1] - c.prevPoints[1];
				if (glm::length(newVel - oldVel) > 0.001f) {
					HT_LOG("  WARNING: Root update changed vertex 1 velocity from (%.3f,%.3f,%.3f) to (%.3f,%.3f,%.3f)\n",
					       oldVel.x, oldVel.y, oldVel.z, newVel.x, newVel.y, newVel.z);
				}
			}
		}
	}
}

static glm::vec3 resampleOnPolyline(const std::vector<glm::vec3>& pts, const std::vector<float>& cumLen, float s) {
	if (pts.size() < 2) return pts.empty() ? glm::vec3(0) : pts[0];
	if (s <= 0.0f) return pts[0];
	float total = cumLen.back();
	if (total <= 1e-8f) return pts[0];
	if (s >= total) return pts.back();

	// Find segment
	size_t hi = 1;
	while (hi < cumLen.size() && cumLen[hi] < s) hi++;
	if (hi >= cumLen.size()) return pts.back();
	size_t lo = hi - 1;
	float a = cumLen[lo];
	float b = cumLen[hi];
	float t = (b > a) ? (s - a) / (b - a) : 0.0f;
	return glm::mix(pts[lo], pts[hi], glm::clamp(t, 0.0f, 1.0f));
}

static void resampleCurveInPlace(HairCurve& c, float newLength, int newSteps) {
	newSteps = glm::clamp(newSteps, 2, 256);
	newLength = glm::max(0.001f, newLength);
	if (c.points.size() < 2) return;

	std::vector<glm::vec3> oldPts = c.points;
	std::vector<float> cum;
	cum.resize(oldPts.size());
	cum[0] = 0.0f;
	for (size_t i = 1; i < oldPts.size(); i++) {
		cum[i] = cum[i - 1] + glm::length(oldPts[i] - oldPts[i - 1]);
	}
	float oldLen = cum.back();

	glm::vec3 root = oldPts[0];
	glm::vec3 lastDir(0, 1, 0);
	if (oldPts.size() >= 2) {
		glm::vec3 d = oldPts.back() - oldPts[oldPts.size() - 2];
		float dl = glm::length(d);
		if (dl > 1e-6f) lastDir = d / dl;
	}

	std::vector<glm::vec3> newPts;
	newPts.resize((size_t)newSteps);
	for (int i = 0; i < newSteps; i++) {
		float t = (newSteps == 1) ? 0.0f : (float)i / (float)(newSteps - 1);
		float targetS = newLength * t;
		glm::vec3 p;
		if (oldLen > 1e-6f) {
			if (targetS <= oldLen) {
				p = resampleOnPolyline(oldPts, cum, targetS);
			} else {
				p = oldPts.back() + lastDir * (targetS - oldLen);
			}
		} else {
			// Degenerate curve: rebuild as a straight line along lastDir.
			p = root + lastDir * targetS;
		}
		newPts[(size_t)i] = p;
	}

	// Preserve root exactly
	newPts[0] = root;
	c.points = std::move(newPts);
	c.prevPoints = c.points;
	c.segmentRestLen = newLength / (float)(newSteps - 1);
}

void HairGuideSet::applyLengthStepsToSelected(float newLength, int newSteps) {
	for (size_t ci = 0; ci < m_curves.size(); ci++) {
		if (!isCurveSelected(ci)) continue;
		resampleCurveInPlace(m_curves[ci], newLength, newSteps);
	}
}
