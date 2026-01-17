#pragma once

#include <string>
#include <memory>
#include <array>

struct GLFWwindow;

class Renderer;
class Scene;
class MayaCameraController;

class App {
public:
	App() = default;
	~App();
	bool init();
	void run();
	void shutdown();

private:
	bool initWindow();
	bool initGL();
	bool initImGui();
	void shutdownImGui();

	void beginFrame();
	void endFrame();

	void drawMenuBar();
	void drawSidePanel();
	void drawLayersPanel();
	void drawControlsOverlay();
	void drawGuideCounterOverlay();
	void drawToastOverlay();
	void handleViewportInput();
	void resetSettingsToDefaults();
	void showToast(const std::string& text, float seconds = 2.0f);

	void actionImportObj();
	void actionSaveScene();
	void actionLoadScene();
	void actionImportCurvesPly();
	void actionExportCurvesPly();

	GLFWwindow* m_window = nullptr;
	int m_windowWidth = 1600;
	int m_windowHeight = 900;
	bool m_windowMaximized = false;

	bool m_shouldClose = false;

	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<MayaCameraController> m_camera;

	// UI state
	bool m_showControlsOverlay = true;
	bool m_showLayersPanel = true;
	float m_uiScale = 1.0f;
	float m_uiScaleApplied = 1.0f;
	std::string m_lastObjPath;
	std::string m_lastScenePath;
	std::string m_lastPlyPath;

	float m_viewportBg[3] = {0.22f, 0.22f, 0.22f};

	// Transient UI message
	std::string m_toastText;
	float m_toastTimeRemaining = 0.0f;

	// UI counters (throttled)
	int m_cachedGuideCount = 0;
	float m_guideCounterAccum = 0.0f;

	// Selection-driven guide settings UI
	uint64_t m_selectedCurvesSignature = 0;
	bool m_selectedLengthMixed = false;
	bool m_selectedStepsMixed = false;

	int m_layerRenameId = -1;
	std::array<char, 64> m_layerRenameBuffer{};
};
