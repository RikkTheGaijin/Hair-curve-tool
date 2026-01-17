#include "App.h"

#include "GL.h"
#include "Renderer.h"
#include "Scene.h"
#include "MayaCameraController.h"
#include "FileDialog.h"
#include "Serialization.h"
#include "ExportPly.h"
#include "ImportPly.h"
#include "GpuSolver.h"
#include "UserSettings.h"

#include "Raycast.h"

#include "HairToolVersion.h"

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

	// Load persistent user settings early so we can restore window size before creating the window.
	UserSettings::load(*m_scene, m_viewportBg, m_showControlsOverlay, m_uiScale, m_windowWidth, m_windowHeight, m_windowMaximized);

	if (!initWindow()) return false;
	if (!initGL()) return false;
	if (!initImGui()) return false;

	m_renderer->init();
	m_camera->setViewport(m_windowWidth, m_windowHeight);
	m_camera->reset();
	// Force style scaling to be applied on the first frame.
	m_uiScaleApplied = 1.0f;

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
	// Show auto-incrementing build version in the title bar.
	{
		std::string title = std::string("Hair Tool v") + HAIRTOOL_VERSION_STRING;
		glfwSetWindowTitle(m_window, title.c_str());
	}
	if (m_windowMaximized) {
		glfwMaximizeWindow(m_window);
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
		if (m_toastTimeRemaining > 0.0f) {
			m_toastTimeRemaining -= ImGui::GetIO().DeltaTime;
			if (m_toastTimeRemaining <= 0.0f) {
				m_toastTimeRemaining = 0.0f;
				m_toastText.clear();
			}
		}
		drawMenuBar();
		drawSidePanel();
		drawControlsOverlay();
		drawGuideCounterOverlay();
		drawToastOverlay();
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

void App::showToast(const std::string& text, float seconds) {
	m_toastText = text;
	m_toastTimeRemaining = std::max(0.0f, seconds);
}

void App::drawToastOverlay() {
	if (m_toastTimeRemaining <= 0.0f || m_toastText.empty()) return;

	ImGuiViewport* vp = ImGui::GetMainViewport();
	// Top-center, inside the work area (below menu bar) so it's never covered.
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 8.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.75f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("##ToastOverlay", nullptr, flags)) {
		ImGui::TextUnformatted(m_toastText.c_str());
	}
	ImGui::End();
}

void App::shutdown() {
	// Save persistent user settings before tearing down.
	int w = m_windowWidth;
	int h = m_windowHeight;
	bool maximized = false;
	if (m_window) {
		glfwGetWindowSize(m_window, &w, &h);
		maximized = (glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) == GLFW_TRUE);
	}
	UserSettings::save(*m_scene, m_viewportBg, m_showControlsOverlay, m_uiScale, w, h, maximized);
	shutdownImGui();
	if (m_window) glfwDestroyWindow(m_window);
	glfwTerminate();
}

void App::beginFrame() {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// UI scaling for high-DPI displays.
	ImGuiIO& io = ImGui::GetIO();
	if (m_uiScale <= 0.01f) m_uiScale = 1.0f;
	io.FontGlobalScale = m_uiScale;
	if (m_uiScaleApplied <= 0.01f) m_uiScaleApplied = 1.0f;
	if (m_uiScaleApplied != m_uiScale) {
		ImGuiStyle& style = ImGui::GetStyle();
		style.ScaleAllSizes(m_uiScale / m_uiScaleApplied);
		m_uiScaleApplied = m_uiScale;
	}
}

void App::endFrame() {
	// No longer used by the main loop. Rendering + swap happen in App::run()
}

void App::drawMenuBar() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Import OBJ...")) actionImportObj();
			if (ImGui::MenuItem("Save Scene...")) actionSaveScene();
			if (ImGui::MenuItem("Load Scene...")) actionLoadScene();
			if (ImGui::MenuItem("Import Curves (PLY)...")) actionImportCurvesPly();
			if (ImGui::MenuItem("Export Curves (PLY)...")) actionExportCurvesPly();
			ImGui::Separator();
			if (ImGui::MenuItem("Quit")) m_shouldClose = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemoWindow);
			ImGui::MenuItem("Controls Help", nullptr, &m_showControlsOverlay);
			if (ImGui::BeginMenu("UI Scale")) {
				bool s1 = (m_uiScale == 1.0f);
				bool s15 = (m_uiScale == 1.5f);
				bool s2 = (m_uiScale == 2.0f);
				if (ImGui::MenuItem("1.0x", nullptr, s1)) m_uiScale = 1.0f;
				if (ImGui::MenuItem("1.5x", nullptr, s15)) m_uiScale = 1.5f;
				if (ImGui::MenuItem("2.0x", nullptr, s2)) m_uiScale = 2.0f;
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
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

void App::drawGuideCounterOverlay() {
	ImGuiViewport* vp = ImGui::GetMainViewport();

	// Refresh the value at a relaxed cadence to avoid needless work.
	m_guideCounterAccum += ImGui::GetIO().DeltaTime;
	if (m_guideCounterAccum >= 1.0f) {
		m_guideCounterAccum = 0.0f;
		m_cachedGuideCount = (int)m_scene->guides().curveCount();
	}

	ImGui::SetNextWindowPos(
		ImVec2(vp->Pos.x + vp->Size.x - 10.0f, vp->Pos.y + vp->Size.y - 10.0f),
		ImGuiCond_Always,
		ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.35f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("##GuideCounter", nullptr, flags)) {
		ImGui::Text("Guides: %d", m_cachedGuideCount);
	}
	ImGui::End();
}

void App::drawSidePanel() {
	ImGuiViewport* vp = ImGui::GetMainViewport();
	// Default: top-right, but the window is still movable; we will clamp to stay visible.
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 10.0f, vp->Pos.y + 60.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Tools & Settings");

	// Keep the window fully inside the visible work area when the main window is resized.
	{
		ImVec2 workMin = vp->WorkPos;
		ImVec2 workMax = ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
		ImVec2 pos = ImGui::GetWindowPos();
		ImVec2 size = ImGui::GetWindowSize();
		float maxX = workMax.x - size.x;
		float maxY = workMax.y - size.y;
		ImVec2 clamped;
		clamped.x = (pos.x < workMin.x) ? workMin.x : ((pos.x > maxX) ? maxX : pos.x);
		clamped.y = (pos.y < workMin.y) ? workMin.y : ((pos.y > maxY) ? maxY : pos.y);
		// If the window is larger than the viewport, pin to top-left.
		if (maxX < workMin.x) clamped.x = workMin.x;
		if (maxY < workMin.y) clamped.y = workMin.y;
		if (clamped.x != pos.x || clamped.y != pos.y) {
			ImGui::SetWindowPos(clamped, ImGuiCond_Always);
		}
	}

	ImGui::TextUnformatted("Mesh");
	ImGui::Separator();
	ImGui::Text("OBJ: %s", m_scene->meshPath().empty() ? "(none)" : m_scene->meshPath().c_str());
	ImGui::Text("Texture: %s", m_scene->meshTexturePath().empty() ? "(none)" : m_scene->meshTexturePath().c_str());
	if (ImGui::Button("Import OBJ")) actionImportObj();
	ImGui::SameLine();
	if (ImGui::Button("Load Texture")) {
		std::string texPath;
		if (FileDialog::openFile(texPath, "Image Files\0*.png;*.jpg;*.jpeg\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0All Files\0*.*\0")) {
			if (m_renderer && m_renderer->loadMeshTexture(texPath)) {
				m_scene->setMeshTexturePath(texPath);
				showToast(std::string("Loaded Texture (") + texPath + ")");
			} else {
				showToast("Failed to load texture");
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Settings")) resetSettingsToDefaults();

	ImGui::Spacing();
	ImGui::TextUnformatted("Guide Settings");
	ImGui::Separator();

	GuideSettings& gs = m_scene->guideSettings();

	// If a curve (or curves) are selected, drive the Length/Steps UI from the selection.
	// If multiple curves have different values, indicate it as "mixed".
	{
		std::vector<int> sel = m_scene->guides().selectedCurves();
		uint64_t sig = 1469598103934665603ull; // FNV-1a 64-bit basis
		for (int idx : sel) {
			sig ^= (uint64_t)(idx + 1);
			sig *= 1099511628211ull;
		}

		m_selectedLengthMixed = false;
		m_selectedStepsMixed = false;

		if (!sel.empty()) {
			const HairCurve& c0 = m_scene->guides().curve((size_t)sel[0]);
			float baseLen = (c0.points.size() >= 2) ? (c0.segmentRestLen * (float)(c0.points.size() - 1)) : gs.defaultLength;
			int baseSteps = (int)c0.points.size();

			for (size_t i = 1; i < sel.size(); i++) {
				const HairCurve& ci = m_scene->guides().curve((size_t)sel[i]);
				float li = (ci.points.size() >= 2) ? (ci.segmentRestLen * (float)(ci.points.size() - 1)) : baseLen;
				int si = (int)ci.points.size();
				if (glm::abs(li - baseLen) > 1e-6f) m_selectedLengthMixed = true;
				if (si != baseSteps) m_selectedStepsMixed = true;
				if (m_selectedLengthMixed && m_selectedStepsMixed) break;
			}

			// Only snap the sliders to the selection when the selection changes,
			// so we don't fight the user's ongoing slider interaction.
			if (sig != m_selectedCurvesSignature) {
				m_selectedCurvesSignature = sig;
				gs.defaultLength = baseLen;
				gs.defaultSteps = baseSteps;
			}
		} else {
			m_selectedCurvesSignature = sig;
		}
	}

	bool lengthChanged = ImGui::SliderFloat("Length", &gs.defaultLength, 0.01f, 2.0f, "%.3f m");
	if (m_selectedLengthMixed) {
		ImGui::SameLine();
		ImGui::TextDisabled("(mixed)");
	}
	bool stepsChanged = ImGui::SliderInt("Steps", &gs.defaultSteps, 2, 64);
	if (m_selectedStepsMixed) {
		ImGui::SameLine();
		ImGui::TextDisabled("(mixed)");
	}
	ImGui::Checkbox("Mirror mode", &gs.mirrorMode);
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
	float dampingAmount = 1.0f - glm::clamp(gs.damping, 0.0f, 1.0f);
	if (ImGui::SliderFloat("Damping", &dampingAmount, 0.0f, 1.0f, "%.3f")) {
		gs.damping = 1.0f - glm::clamp(dampingAmount, 0.0f, 1.0f);
	}
	ImGui::SliderFloat("Bend Stiffness", &gs.stiffness, 0.0f, 1.0f, "%.2f");
	float dragSmooth = 1.0f - glm::clamp(gs.dragLerp, 0.0f, 1.0f);
	if (ImGui::SliderFloat("Drag Smooth", &dragSmooth, 0.0f, 1.0f, "%.2f")) {
		gs.dragLerp = 1.0f - glm::clamp(dragSmooth, 0.0f, 1.0f);
	}

	ImGui::Spacing();
	ImGui::TextUnformatted("Viewport");
	ImGui::Separator();
	ImGui::ColorEdit3("Background", m_viewportBg);
	ImGui::Checkbox("Show Grid", &m_scene->renderSettings().showGrid);
	ImGui::Checkbox("Show Mesh", &m_scene->renderSettings().showMesh);
	ImGui::Checkbox("Show Guides", &m_scene->renderSettings().showGuides);
	ImGui::SliderFloat("Deselected Opacity", &m_scene->renderSettings().deselectedCurveOpacity, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat("Guide Point Size", &m_scene->renderSettings().guidePointSizePx, 1.0f, 16.0f, "%.1f px");

	ImGui::End();
}

void App::handleViewportInput() {
	ImGuiIO& io = ImGui::GetIO();
	// Don't handle camera input if ImGui wants the mouse (over a window/menu)
	if (io.WantCaptureMouse) return;

	// Temporary gravity override while holding G
	// Allow this even when ImGui wants keyboard navigation; only disable while typing into a text field.
	if (!io.WantTextInput) {
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

	// Keyboard actions (avoid triggering while typing into a text field)
	if (!io.WantTextInput) {
		// Quick save scene
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
			if (m_lastScenePath.empty()) {
				actionSaveScene();
				if (!m_lastScenePath.empty()) {
					showToast(std::string("Quick Save (") + m_lastScenePath + ")");
				}
			} else {
				Serialization::saveScene(*m_scene, *m_camera, m_lastScenePath);
				showToast(std::string("Quick Save (") + m_lastScenePath + ")");
			}
		}

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
	m_uiScale = 1.0f;
}

void App::actionImportObj() {
	std::string path;
	if (!FileDialog::openFile(path, "OBJ Files\0*.obj\0All Files\0*.*\0")) return;
	m_lastObjPath = path;
	m_scene->loadMeshFromObj(path);
	if (m_renderer) m_renderer->clearMeshTexture();
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
	Serialization::saveScene(*m_scene, *m_camera, path);
}

void App::actionLoadScene() {
	std::string path;
	if (!FileDialog::openFile(path, "Scene Files\0*.json\0All Files\0*.*\0")) return;
	m_lastScenePath = path;
	bool cameraRestored = false;
	Serialization::loadScene(*m_scene, m_camera.get(), path, &cameraRestored);
	if (m_renderer) {
		m_renderer->clearMeshTexture();
		if (!m_scene->meshTexturePath().empty()) {
			m_renderer->loadMeshTexture(m_scene->meshTexturePath());
		}
	}
	// User preference: nothing selected after loading.
	m_scene->guides().deselectAll();
	// Fallback behavior for older scenes without saved camera state.
	if (!cameraRestored && m_scene->mesh()) {
		m_camera->frameBounds(m_scene->meshBoundsMin(), m_scene->meshBoundsMax());
	}
}

void App::actionImportCurvesPly() {
	std::string path;
	if (!FileDialog::openFile(path, "PLY Files\0*.ply\0All Files\0*.*\0")) return;

	// Ensure the file has a .ply extension (Windows file dialog can return paths without extension).
	{
		std::filesystem::path p(path);
		std::string ext = p.extension().string();
		std::string extLower = ext;
		std::transform(extLower.begin(), extLower.end(), extLower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (extLower.empty()) {
			p += ".ply";
		} else if (extLower != ".ply") {
			p.replace_extension(".ply");
		}
		path = p.string();
	}

	std::vector<ImportPly::ImportedCurve> curves;
	std::string err;
	if (!ImportPly::loadCurves(path, curves, &err)) {
		showToast(std::string("Import Curves failed: ") + (err.empty() ? "invalid PLY" : err));
		return;
	}

	if (!m_scene->mesh()) {
		showToast("Import Curves failed: no mesh loaded");
		return;
	}

	// Replace current curves with imported ones.
	m_scene->clearCurves();

	const Mesh& mesh = *m_scene->mesh();
	const GuideSettings& gs = m_scene->guideSettings();

	int droppedNoBinding = 0;
	int droppedInvalid = 0;
	int imported = 0;

	// Tolerance for snapping roots to a potentially different mesh.
	// In HairTool, a valid exported root should be essentially on the surface; keep this tight
	// so importing curves against a significantly different mesh drops unbindable strands.
	const float maxBindDist = std::max(0.005f, gs.collisionThickness * 2.0f);

	for (const auto& ic : curves) {
		if (ic.points.size() < 2) {
			droppedInvalid++;
			continue;
		}
		int rootIdx = (ic.anchorIndex >= 0 && (size_t)ic.anchorIndex < ic.points.size()) ? ic.anchorIndex : 0;
		std::vector<glm::vec3> pts = ic.points;
		if (rootIdx != 0) {
			// Ensure the root is point[0] (HairTool treats index 0 as the pinned root).
			std::rotate(pts.begin(), pts.begin() + rootIdx, pts.end());
			rootIdx = 0;
		}
		glm::vec3 rootPos = pts[(size_t)rootIdx];

		RayHit hit;
		if (!Raycast::nearestOnMesh(mesh, rootPos, hit, maxBindDist) || !hit.hit || hit.triIndex < 0) {
			droppedNoBinding++;
			continue;
		}

		// Append via addCurveOnMesh (to preserve invariants), then overwrite with imported points.
		m_scene->guides().addCurveOnMesh(mesh, hit.triIndex, hit.bary, hit.position, hit.normal, gs);
		HairCurve& dst = m_scene->guides().curve(m_scene->guides().curveCount() - 1);
		dst.root.triIndex = hit.triIndex;
		dst.root.bary = hit.bary;
		dst.points = pts;
		dst.prevPoints = pts;
		// Snap the root to the bound mesh point.
		if (!dst.points.empty()) {
			dst.points[0] = hit.position;
			dst.prevPoints[0] = hit.position;
		}
		if (dst.points.size() >= 2) {
			float sum = 0.0f;
			for (size_t si = 0; si + 1 < dst.points.size(); si++) sum += glm::length(dst.points[si + 1] - dst.points[si]);
			dst.segmentRestLen = sum / (float)(dst.points.size() - 1);
		}
		imported++;
	}

	m_scene->guides().deselectAll();

	if (droppedNoBinding > 0) {
		showToast(
			std::to_string(droppedNoBinding) + " curves cannot find a binding surface (dropped). Imported " + std::to_string(imported),
			5.0f
		);
	} else if (droppedInvalid > 0) {
		showToast(
			"Imported Curves (PLY): " + std::to_string(imported) + " (dropped " + std::to_string(droppedInvalid) + " invalid)",
			5.0f
		);
	} else {
		showToast("Imported Curves (PLY): " + std::to_string(imported) + " (dropped 0)", 5.0f);
	}
}

void App::actionExportCurvesPly() {
	std::string path;
	if (!FileDialog::saveFile(path, "PLY Files\0*.ply\0All Files\0*.*\0")) return;
	// Ensure the file has a .ply extension (Windows file dialog can return paths without extension).
	{
		std::filesystem::path p(path);
		std::string ext = p.extension().string();
		std::string extLower = ext;
		std::transform(extLower.begin(), extLower.end(), extLower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (extLower.empty()) {
			p += ".ply";
		} else if (extLower != ".ply") {
			p.replace_extension(".ply");
		}
		path = p.string();
	}
	m_lastPlyPath = path;
	ExportPly::exportCurvesAsPointCloud(*m_scene, path);
	showToast(std::string("Exported PLY (") + path + ")");
}
