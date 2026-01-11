#pragma once

#include <string>
#include <memory>

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
	void drawControlsOverlay();
	void handleViewportInput();
	void resetSettingsToDefaults();

	void actionImportObj();
	void actionSaveScene();
	void actionLoadScene();
	void actionExportCurvesPly();

	GLFWwindow* m_window = nullptr;
	int m_windowWidth = 1600;
	int m_windowHeight = 900;

	bool m_shouldClose = false;

	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<MayaCameraController> m_camera;

	// UI state
	bool m_showDemoWindow = false;
	bool m_showControlsOverlay = true;
	std::string m_lastObjPath;
	std::string m_lastScenePath;
	std::string m_lastPlyPath;

	float m_viewportBg[3] = {0.22f, 0.22f, 0.22f};
};
