#pragma once

#include "HairGuides.h"
#include "Mesh.h"
#include "MeshDistanceField.h"

#include <memory>
#include <string>

#include <unordered_map>

class MayaCameraController;

struct RenderSettings {
	bool showGrid = true;
	bool showMesh = true;
	bool showGuides = true;
	float guidePointSizePx = 6.0f;
};

class Scene {
public:
	Scene();

	bool loadMeshFromObj(const std::string& path);
	const std::string& meshPath() const { return m_meshPath; }
	const std::string& meshTexturePath() const { return m_meshTexturePath; }
	void setMeshTexturePath(const std::string& path) { m_meshTexturePath = path; }
	const Mesh* mesh() const { return m_mesh.get(); }
	Mesh* mesh() { return m_mesh.get(); }

	const glm::vec3& meshBoundsMin() const { return m_meshBoundsMin; }
	const glm::vec3& meshBoundsMax() const { return m_meshBoundsMax; }
	uint64_t meshVersion() const { return m_meshVersion; }
	const MeshDistanceField& meshDistanceField() const { return m_meshField; }

	HairGuideSet& guides() { return m_guides; }
	const HairGuideSet& guides() const { return m_guides; }

	GuideSettings& guideSettings() { return m_guideSettings; }
	const GuideSettings& guideSettings() const { return m_guideSettings; }

	RenderSettings& renderSettings() { return m_renderSettings; }
	const RenderSettings& renderSettings() const { return m_renderSettings; }

	void tick();
	void simulate(float dt);

	void handleViewportMouse(const MayaCameraController& camera, int viewportW, int viewportH);
	void deleteSelectedCurves();
	void resetSettingsToDefaults();

	void setGravityOverrideHeld(bool held) { m_gravityOverrideHeld = held; }
	bool gravityOverrideHeld() const { return m_gravityOverrideHeld; }
	float gravityOverrideValue() const { return m_gravityOverrideValue; }
	float effectiveGravityForCurve(size_t curveIdx) const;

	int hoverCurve() const { return m_hoverCurve; }
	bool hoverHighlightActive() const { return m_hoverHighlightActive; }

	// Interaction state accessors (used by physics to stabilize dragging)
	bool isDragging() const { return m_dragging; }
	int dragCurve() const { return m_dragCurve; }
	int dragVert() const { return m_dragVert; }

private:
	std::unique_ptr<Mesh> m_mesh;
	std::string m_meshPath;
	std::string m_meshTexturePath;
	glm::vec3 m_meshBoundsMin{0.0f};
	glm::vec3 m_meshBoundsMax{0.0f};
	uint64_t m_meshVersion = 0;
	MeshDistanceField m_meshField;

	HairGuideSet m_guides;
	GuideSettings m_guideSettings;
	RenderSettings m_renderSettings;

	// Interaction state
	int m_dragCurve = -1;
	int m_dragVert = -1;
	bool m_dragging = false;
	glm::vec3 m_dragPlanePoint{0.0f};
	glm::vec3 m_dragPlaneNormal{0.0f, 0.0f, 1.0f};
	int m_hoverCurve = -1;
	bool m_hoverHighlightActive = false;

	bool m_gravityOverrideHeld = false;
	float m_gravityOverrideValue = 9.81f;

	// Mirror mode is transient: only applies to curves created while enabled, and only while they remain selected.
	std::unordered_map<int, int> m_mirrorPeer;

	static glm::vec3 mirrorX(const glm::vec3& p);
	void clearMirrorPairs();
	void pruneMirrorPairsToSelection();
	int mirrorPeerOf(int curveIdx) const;
	void setMirrorPair(int a, int b);
	void clearMirrorPairFor(int curveIdx);

	void beginDragVertex(const MayaCameraController& camera, int viewportW, int viewportH);
	void updateDragVertex(const MayaCameraController& camera, int viewportW, int viewportH);
	void endDragVertex();
};
