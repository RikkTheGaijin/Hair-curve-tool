#include "App.h"

#include "GL.h"
#include "Renderer.h"
#include "Scene.h"
#include "MayaCameraController.h"
#include "FileDialog.h"
#include "Serialization.h"
#include "ExportPly.h"
#include "GpuSolver.h"
#include "UserSettings.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cctype>

App::~App() = default;

static void glfwErrorCallback(int error, const char* description) {
	std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

bool App::init() {
	m_scene = std::make_unique<Scene>();
	m_camera = std::make_unique<MayaCameraController>();
	m_renderer = std::make_unique<Renderer>();

	if (!initWindow()) return false;
	if (!initGL()) return false;
	if (!initImGui()) return false;

	m_renderer->init();
	m_camera->setViewport(m_windowWidth, m_windowHeight);
	m_camera->reset();

	// Persistent user settings
	UserSettings::load(*m_scene, m_viewportBg, m_showControlsOverlay);

	return true;
}

bool App::initWindow() {
	glfwSetErrorCallback(glfwErrorCallback);

	if (!glfwInit()) {
		std::fprintf(stderr, "Failed to init GLFW\n");
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef _DEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

	m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "Hair Tool", nullptr, nullptr);
	if (!m_window) {
		std::fprintf(stderr, "Failed to create GLFW window\n");
		return false;
	}

	glfwMakeContextCurrent(m_window);
	glfwSwapInterval(1);
	return true;
}

bool App::initGL() {
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::fprintf(stderr, "Failed to load GLAD\n");
		return false;
	}

#ifdef HAIRTOOL_ENABLE_GL_DEBUG
	GL::enableDebugOutput();
#endif

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	return true;
}

bool App::initImGui() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// ViewportsEnable disabled to keep viewport docked in main window
	// io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 6.0f;
	style.FrameRounding = 4.0f;
	style.GrabRounding = 4.0f;

	if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true)) return false;
	if (!ImGui_ImplOpenGL3_Init("#version 330")) return false;

	return true;
}

void App::shutdownImGui() {
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void App::run() {
	while (!glfwWindowShouldClose(m_window) && !m_shouldClose) {
		glfwPollEvents();

		int w = 0, h = 0;
		glfwGetFramebufferSize(m_window, &w, &h);
		m_windowWidth = w;
		m_windowHeight = h;
		if (w != m_camera->viewportWidth() || h != m_camera->viewportHeight()) {
			m_camera->setViewport(w, h);
		}

		// Start UI frame early so input is up-to-date before simulation.
		beginFrame();
		drawMenuBar();
		drawSidePanel();
		drawControlsOverlay();
		handleViewportInput();
		ImGui::Render();

		m_scene->tick();
		// Use YarnBall's timestep approach: max 1ms per substep for stability
		const float dt = 1.0f / 60.0f;
		const float maxSubstep = 0.001f;  // YarnBall's maxH
		int substeps = (int)glm::ceil(dt / maxSubstep);
		for (int i = 0; i < substeps; i++) {
			m_scene->simulate(dt / (float)substeps);
		}

		// Render scene directly to main window framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, w, h);
		glClearColor(m_viewportBg[0], m_viewportBg[1], m_viewportBg[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		m_renderer->render(*m_scene, *m_camera);

		// Draw ImGui overlay AFTER 3D rendering
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(m_window);
	}
}

void App::shutdown() {
	// Save persistent user settings before tearing down.
	UserSettings::save(*m_scene, m_viewportBg, m_showControlsOverlay);
	shutdownImGui();
	if (m_window) glfwDestroyWindow(m_window);
	glfwTerminate();
}

void App::beginFrame() {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void App::endFrame() {
	// No longer used by the main loop. Rendering + swap happen in App::run()
}

void App::drawMenuBar() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	if (ImGui::Begin("##MainMenuBar", nullptr, flags)) {
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Import OBJ...")) actionImportObj();
				if (ImGui::MenuItem("Save Scene...")) actionSaveScene();
				if (ImGui::MenuItem("Load Scene...")) actionLoadScene();
				if (ImGui::MenuItem("Export Curves (PLY)...")) actionExportCurvesPly();
				ImGui::Separator();
				if (ImGui::MenuItem("Quit")) m_shouldClose = true;
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View")) {
				ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemoWindow);
				ImGui::MenuItem("Controls Help", nullptr, &m_showControlsOverlay);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void App::drawControlsOverlay() {
	if (!m_showControlsOverlay) return;

	ImGuiViewport* vp = ImGui::GetMainViewport();
	// Bottom-left, with a small margin.
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 10.0f, vp->Pos.y + vp->Size.y - 10.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.35f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("##ControlsOverlay", nullptr, flags)) {
		ImGui::TextUnformatted("Controls");
		ImGui::Separator();
		ImGui::BulletText("MMB: Create new curve on mesh");
		ImGui::BulletText("LMB: Drag selected curve vertices");
		ImGui::BulletText("SHIFT + LMB: Select single curve");
		ImGui::BulletText("SHIFT + CTRL + LMB: Add to selection (active)");
		ImGui::BulletText("SHIFT (hover): Highlight curve (red)");
		ImGui::BulletText("SHIFT + MMB (empty): Deselect all");
		ImGui::BulletText("DEL: Delete selected curve(s)");
		ImGui::BulletText("Hold G: Temporary gravity override");
		ImGui::BulletText("ALT + LMB/MMB/RMB: Camera orbit/pan/zoom");
	}
	ImGui::End();
}

void App::drawSidePanel() {
	ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Tools & Settings");

	ImGui::TextUnformatted("Mesh");
	ImGui::Separator();
	ImGui::Text("OBJ: %s", m_scene->meshPath().empty() ? "(none)" : m_scene->meshPath().c_str());
	if (ImGui::Button("Import OBJ")) actionImportObj();
	ImGui::SameLine();
	if (ImGui::Button("Reset Camera")) m_camera->reset();
	ImGui::SameLine();
	if (ImGui::Button("Reset Settings")) resetSettingsToDefaults();

	ImGui::Spacing();
	ImGui::TextUnformatted("Guide Settings");
	ImGui::Separator();

	GuideSettings& gs = m_scene->guideSettings();
	bool lengthChanged = ImGui::SliderFloat("Length", &gs.defaultLength, 0.01f, 2.0f, "%.3f m");
	bool stepsChanged = ImGui::SliderInt("Steps", &gs.defaultSteps, 2, 64);
	if (lengthChanged || stepsChanged) {
		m_scene->guides().applyLengthStepsToSelected(gs.defaultLength, gs.defaultSteps);
	}
	
	ImGui::Spacing();
	ImGui::TextUnformatted("Simulation");
	ImGui::Separator();
	ImGui::Checkbox("Enable Physics Simulation", &gs.enableSimulation);
	// GPU solver toggle intentionally hidden for now (CPU is the primary workflow)
	ImGui::Checkbox("Enable Mesh Collision", &gs.enableMeshCollision);
	ImGui::Checkbox("Enable Curve Collision", &gs.enableCurveCollision);
	ImGui::SliderFloat("Collision Thickness", &gs.collisionThickness, 0.0001f, 0.02f, "%.4f m");
	ImGui::SliderFloat("Friction", &gs.collisionFriction, 0.0f, 1.0f, "%.2f");
	ImGui::SliderInt("Solver Iterations", &gs.solverIterations, 1, 32);
	ImGui::SliderFloat("Gravity", &gs.gravity, 0.0f, 30.0f, "%.2f m/s^2");
	ImGui::SliderFloat("Damping", &gs.damping, 0.90f, 1.0f, "%.3f");
	ImGui::SliderFloat("Bend Stiffness", &gs.stiffness, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("Drag Smooth", &gs.dragLerp, 0.05f, 1.0f, "%.2f");

	ImGui::Spacing();
	ImGui::TextUnformatted("Viewport");
	ImGui::Separator();
	ImGui::ColorEdit3("Background", m_viewportBg);
	ImGui::Checkbox("Show Grid", &m_scene->renderSettings().showGrid);
	ImGui::Checkbox("Show Mesh", &m_scene->renderSettings().showMesh);
	ImGui::Checkbox("Show Guides", &m_scene->renderSettings().showGuides);
	ImGui::SliderFloat("Guide Point Size", &m_scene->renderSettings().guidePointSizePx, 1.0f, 16.0f, "%.1f px");

	ImGui::End();
}

void App::handleViewportInput() {
	ImGuiIO& io = ImGui::GetIO();
	// Don't handle camera input if ImGui wants the mouse (over a window/menu)
	if (io.WantCaptureMouse) return;

	// Temporary gravity override while holding G
	if (!io.WantCaptureKeyboard) {
		m_scene->setGravityOverrideHeld(ImGui::IsKeyDown(ImGuiKey_G));
	} else {
		m_scene->setGravityOverrideHeld(false);
	}

	const bool alt = io.KeyAlt;
	const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
	const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);

	m_camera->handleMouse(alt, lmb, mmb, rmb, (float)io.MouseDelta.x, (float)io.MouseDelta.y, io.MouseWheel);

	// Click-to-spawn / select / drag (only when not navigating)
	if (!alt) {
		m_scene->handleViewportMouse(*m_camera, m_windowWidth, m_windowHeight);
	}

	// Keyboard actions (only when ImGui doesn't want the keyboard)
	if (!io.WantCaptureKeyboard) {
		if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
			m_scene->deleteSelectedCurves();
		}
	}
}

void App::resetSettingsToDefaults() {
	m_scene->resetSettingsToDefaults();
	m_viewportBg[0] = 0.22f;
	m_viewportBg[1] = 0.22f;
	m_viewportBg[2] = 0.22f;
	m_showControlsOverlay = true;
	m_showDemoWindow = false;
}

void App::actionImportObj() {
	std::string path;
	if (!FileDialog::openFile(path, "OBJ Files\0*.obj\0All Files\0*.*\0")) return;
	m_lastObjPath = path;
	m_scene->loadMeshFromObj(path);
	m_camera->frameBounds(m_scene->meshBoundsMin(), m_scene->meshBoundsMax());
}

void App::actionSaveScene() {
	std::string path;
	if (!FileDialog::saveFile(path, "Scene Files\0*.json\0All Files\0*.*\0")) return;
	// Ensure the file has a .json extension (Windows file dialog can return paths without extension).
	{
		std::filesystem::path p(path);
		std::string ext = p.extension().string();
		std::string extLower = ext;
		std::transform(extLower.begin(), extLower.end(), extLower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (extLower.empty()) {
			p += ".json";
		} else if (extLower != ".json") {
			p.replace_extension(".json");
		}
		path = p.string();
	}
	m_lastScenePath = path;
	Serialization::saveScene(*m_scene, path);
}

void App::actionLoadScene() {
	std::string path;
	if (!FileDialog::openFile(path, "Scene Files\0*.json\0All Files\0*.*\0")) return;
	m_lastScenePath = path;
	Serialization::loadScene(*m_scene, path);
	// After loading, select the first curve (if any) so vertices are visible and physics only affects a small subset by default.
	if (m_scene->guides().curveCount() > 0) {
		m_scene->guides().selectCurve(0, false);
	}
	m_camera->frameBounds(m_scene->meshBoundsMin(), m_scene->meshBoundsMax());
}

void App::actionExportCurvesPly() {
	std::string path;
	if (!FileDialog::saveFile(path, "PLY Files\0*.ply\0All Files\0*.*\0")) return;
	m_lastPlyPath = path;
	ExportPly::exportCurvesAsPointCloud(*m_scene, path);
}
