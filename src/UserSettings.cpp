#include "UserSettings.h"

#include "Scene.h"

#include <json/json.h>

#include <fstream>
#include <filesystem>
#include <cstdlib>

static std::filesystem::path settingsFilePath() {
	char* appdata = nullptr;
	size_t len = 0;
	_dupenv_s(&appdata, &len, "APPDATA");
	std::filesystem::path base;
	if (appdata && appdata[0]) {
		base = std::filesystem::path(appdata) / "HairTool";
	} else {
		base = std::filesystem::current_path();
	}
	if (appdata) {
		free(appdata);
	}
	return base / "settings.json";
}

std::string UserSettings::settingsPath() {
	return settingsFilePath().string();
}

bool UserSettings::load(Scene& scene, float viewportBg[3], bool& showControlsOverlay, bool& showLayersPanel, float& uiScale, int& windowWidth, int& windowHeight, bool& windowMaximized) {
	std::filesystem::path path = settingsFilePath();
	if (!std::filesystem::exists(path)) return false;

	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) return false;

	Json::CharReaderBuilder rb;
	Json::Value root;
	std::string errs;
	if (!Json::parseFromStream(rb, f, &root, &errs)) {
		return false;
	}

	// Guide settings
	GuideSettings& gs = scene.guideSettings();
	Json::Value jgs = root["guideSettings"];
	if (jgs.isObject()) {
		gs.defaultLength = jgs.get("defaultLength", gs.defaultLength).asFloat();
		gs.defaultSteps = jgs.get("defaultSteps", gs.defaultSteps).asInt();
		gs.enableSimulation = jgs.get("enableSimulation", gs.enableSimulation).asBool();
		gs.enableMeshCollision = jgs.get("enableMeshCollision", gs.enableMeshCollision).asBool();
		gs.enableCurveCollision = jgs.get("enableCurveCollision", gs.enableCurveCollision).asBool();
		gs.enableGpuSolver = jgs.get("enableGpuSolver", gs.enableGpuSolver).asBool();
		gs.collisionThickness = jgs.get("collisionThickness", gs.collisionThickness).asFloat();
		gs.collisionFriction = jgs.get("collisionFriction", gs.collisionFriction).asFloat();
		gs.solverIterations = jgs.get("solverIterations", gs.solverIterations).asInt();
		gs.gravity = jgs.get("gravity", gs.gravity).asFloat();
		gs.damping = jgs.get("damping", gs.damping).asFloat();
		gs.stiffness = jgs.get("stiffness", gs.stiffness).asFloat();
		gs.dragLerp = jgs.get("dragLerp", gs.dragLerp).asFloat();
	}

	// Render settings
	RenderSettings& rs = scene.renderSettings();
	Json::Value jrs = root["renderSettings"];
	if (jrs.isObject()) {
		rs.showGrid = jrs.get("showGrid", rs.showGrid).asBool();
		rs.showMesh = jrs.get("showMesh", rs.showMesh).asBool();
		rs.showGuides = jrs.get("showGuides", rs.showGuides).asBool();
		rs.deselectedCurveOpacity = jrs.get("deselectedCurveOpacity", rs.deselectedCurveOpacity).asFloat();
		rs.guidePointSizePx = jrs.get("guidePointSizePx", rs.guidePointSizePx).asFloat();
	}

	// UI
	Json::Value ui = root["ui"];
	if (ui.isObject()) {
		showControlsOverlay = ui.get("showControlsOverlay", showControlsOverlay).asBool();
		showLayersPanel = ui.get("showLayersPanel", showLayersPanel).asBool();
		uiScale = ui.get("uiScale", uiScale).asFloat();
		windowWidth = ui.get("windowWidth", windowWidth).asInt();
		windowHeight = ui.get("windowHeight", windowHeight).asInt();
		windowMaximized = ui.get("windowMaximized", windowMaximized).asBool();
	}

	// Viewport background
	Json::Value bg = root["viewportBg"];
	if (bg.isArray() && bg.size() == 3) {
		viewportBg[0] = bg[0].asFloat();
		viewportBg[1] = bg[1].asFloat();
		viewportBg[2] = bg[2].asFloat();
	}

	return true;
}

bool UserSettings::save(const Scene& scene, const float viewportBg[3], bool showControlsOverlay, bool showLayersPanel, float uiScale, int windowWidth, int windowHeight, bool windowMaximized) {
	std::filesystem::path path = settingsFilePath();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	Json::Value root;
	root["version"] = 1;

	// Guide settings
	const GuideSettings& gs = scene.guideSettings();
	Json::Value jgs;
	jgs["defaultLength"] = gs.defaultLength;
	jgs["defaultSteps"] = gs.defaultSteps;
	jgs["enableSimulation"] = gs.enableSimulation;
	jgs["enableMeshCollision"] = gs.enableMeshCollision;
	jgs["enableCurveCollision"] = gs.enableCurveCollision;
	jgs["enableGpuSolver"] = gs.enableGpuSolver;
	jgs["collisionThickness"] = gs.collisionThickness;
	jgs["collisionFriction"] = gs.collisionFriction;
	jgs["solverIterations"] = gs.solverIterations;
	jgs["gravity"] = gs.gravity;
	jgs["damping"] = gs.damping;
	jgs["stiffness"] = gs.stiffness;
	jgs["dragLerp"] = gs.dragLerp;
	root["guideSettings"] = jgs;

	// Render settings
	const RenderSettings& rs = scene.renderSettings();
	Json::Value jrs;
	jrs["showGrid"] = rs.showGrid;
	jrs["showMesh"] = rs.showMesh;
	jrs["showGuides"] = rs.showGuides;
	jrs["deselectedCurveOpacity"] = rs.deselectedCurveOpacity;
	jrs["guidePointSizePx"] = rs.guidePointSizePx;
	root["renderSettings"] = jrs;

	// UI
	Json::Value ui;
	ui["showControlsOverlay"] = showControlsOverlay;
	ui["showLayersPanel"] = showLayersPanel;
	ui["uiScale"] = uiScale;
	ui["windowWidth"] = windowWidth;
	ui["windowHeight"] = windowHeight;
	ui["windowMaximized"] = windowMaximized;
	root["ui"] = ui;

	Json::Value bg(Json::arrayValue);
	bg.append(viewportBg[0]);
	bg.append(viewportBg[1]);
	bg.append(viewportBg[2]);
	root["viewportBg"] = bg;

	Json::StreamWriterBuilder wb;
	wb["indentation"] = "  ";
	std::unique_ptr<Json::StreamWriter> writer(wb.newStreamWriter());

	std::ofstream f(path, std::ios::binary);
	if (!f.is_open()) return false;
	writer->write(root, &f);
	return true;
}
