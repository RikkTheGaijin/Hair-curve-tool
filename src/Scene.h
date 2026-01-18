#pragma once

#include "HairGuides.h"
#include "Mesh.h"
#include "MeshDistanceField.h"

#include <memory>
#include <string>
#include <vector>

#include <unordered_map>

class MayaCameraController;

struct RenderSettings {
	bool showGrid = true;
	bool showMesh = true;
	bool showGuides = true;
	bool showHair = true;
	int msaaSamples = 4;
	float guidePointSizePx = 6.0f;
	float deselectedCurveOpacity = 1.0f;
};

enum class ModuleType {
	Curves = 0,
	Hair = 1
};

enum class HairDistributionType {
	Uniform = 0,
	Vertex = 1,
	Even = 2
};

struct HairSettings {
	int hairCount = 20000;
	HairDistributionType distribution = HairDistributionType::Uniform;
	float rootThickness = 0.0010f;
	float midThickness = 0.0050f;
	float tipThickness = 0.0001f;
	float rootExtent = 0.005f; // meters from root to reach mid thickness
	float tipExtent = 0.005f;  // meters from tip to taper to tip thickness
	std::string distributionMaskPath;
	std::string lengthMaskPath;
};

struct HairRenderData {
	std::vector<float> vertices; // pos3, tangent3, t, side
	std::vector<uint32_t> indices;
};

struct MaskData {
	int w = 0;
	int h = 0;
	std::vector<unsigned char> pixels; // BGRA8
	bool valid() const { return w > 0 && h > 0 && !pixels.empty(); }
};

struct LayerInfo {
	std::string name;
	glm::vec3 color{0.90f, 0.75f, 0.22f};
	bool visible = true;
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

	ModuleType activeModule() const { return m_activeModule; }
	void setActiveModule(ModuleType t) { m_activeModule = t; }

	HairSettings& hairSettings() { return m_hairSettings; }
	const HairSettings& hairSettings() const { return m_hairSettings; }

	bool loadHairDistributionMask(const std::string& path);
	bool loadHairLengthMask(const std::string& path);
	void clearHairMasks();

	void buildHairRenderData(HairRenderData& out) const;
	int lastHairCount() const { return m_lastHairCount; }

	// Layers
	int activeLayer() const { return m_activeLayer; }
	size_t layerCount() const { return m_layers.size(); }
	const LayerInfo& layer(size_t idx) const { return m_layers[idx]; }
	LayerInfo& layer(size_t idx) { return m_layers[idx]; }
	void setLayers(const std::vector<LayerInfo>& layers, int activeLayer);
	int addLayer(const std::string& name, const glm::vec3& color, bool visible = true);
	bool deleteLayer(int layerId);
	void setActiveLayer(int layerId);
	void setLayerVisible(int layerId, bool visible);
	void setLayerColor(int layerId, const glm::vec3& color);
	bool isLayerVisible(int layerId) const;
	glm::vec3 generateDistinctLayerColor();

	void tick();
	void simulate(float dt);

	void handleViewportMouse(const MayaCameraController& camera, int viewportW, int viewportH);
	void deleteSelectedCurves();
	void resetSettingsToDefaults();
	void clearCurves();

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
	ModuleType m_activeModule = ModuleType::Curves;
	HairSettings m_hairSettings;

	std::vector<LayerInfo> m_layers;
	int m_activeLayer = 0;

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

	MaskData m_distMask;
	MaskData m_lenMask;
	mutable int m_lastHairCount = 0;

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
	void resetLayers();
	void refreshCurveLayerProperties();
};
