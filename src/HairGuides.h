#pragma once

#include <glm/glm.hpp>

#include <vector>

class Mesh;

struct GuideSettings {
	float defaultLength = 0.3f;
	int defaultSteps = 12;
	bool mirrorMode = false;
	bool enableSimulation = false;       // Master simulation toggle
	bool enableMeshCollision = true;
	bool enableCurveCollision = false;
	bool enableGpuSolver = false;
	float collisionThickness = 0.0020f;
	// Friction applied on mesh collision: 0 = slide freely, 1 = fully sticky
	float collisionFriction = 1.0f;
	int solverIterations = 12;
	float gravity = 0.0f;               // m/s^2 (world units are meters)
	float damping = 0.900f;             // Verlet velocity damping [0..1]
	float stiffness = 0.10f;            // Distance constraint stiffness [0..1]
	float dragLerp = 0.35f;             // Mouse drag smoothing [0..1] (higher = snappier)
};

struct HairRootBinding {
	int triIndex = -1;
	glm::vec3 bary{0.0f};
};

struct HairCurve {
	HairRootBinding root;
	std::vector<glm::vec3> points;      // control points used for physics
	std::vector<glm::vec3> prevPoints;  // for verlet
	float segmentRestLen = 0.0f;
};

class HairGuideSet {
public:
	void clear();

	size_t curveCount() const { return m_curves.size(); }
	const HairCurve& curve(size_t idx) const { return m_curves[idx]; }
	HairCurve& curve(size_t idx) { return m_curves[idx]; }

	// Returns the new curve index, or -1 on failure.
	int addCurveOnMesh(const Mesh& mesh, int triIndex, const glm::vec3& bary, const glm::vec3& hitPos, const glm::vec3& hitNormal, const GuideSettings& settings);

	// Selection / active curve
	bool isCurveSelected(size_t curveIdx) const;
	int activeCurve() const { return m_activeCurve; }
	void deselectAll();
	void selectCurve(int curveIdx, bool additive);
	void toggleCurveSelected(int curveIdx);
	std::vector<int> selectedCurves() const;
	void applyLengthStepsToSelected(float newLength, int newSteps);

	// Interaction
	bool pickControlPoint(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& camPos, const glm::mat4& viewProj, int& outCurve, int& outVert, bool selectedOnly = false) const;
	bool pickCurve(const glm::vec3& ro, const glm::vec3& rd, int& outCurve) const;
	void moveControlPoint(int curveIdx, int vertIdx, const glm::vec3& worldPos);
	void removeCurve(int curveIdx);
	void removeCurves(const std::vector<int>& curveIndicesDescending);

	// Rendering
	void drawDebugLines(const glm::mat4& viewProj, unsigned int lineProgram, float pointSizePx, int hoverCurve = -1, bool hoverHighlightRed = false) const;

	// Simulation helpers
	void updatePinnedRootsFromMesh(const Mesh& mesh);

private:
	std::vector<HairCurve> m_curves;
	std::vector<unsigned char> m_selected; // 1 if selected
	int m_activeCurve = -1;

	static glm::vec3 evalCatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t);
	static void buildCurveRenderPoints(const HairCurve& c, std::vector<glm::vec3>& outPoints);
};
