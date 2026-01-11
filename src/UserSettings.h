#pragma once

#include <string>

class Scene;

namespace UserSettings {
	// Loads persistent user settings (tool UI + guide/render settings).
	// Returns true if a settings file was found and parsed.
	bool load(Scene& scene, float viewportBg[3], bool& showControlsOverlay, float& uiScale, int& windowWidth, int& windowHeight, bool& windowMaximized);

	// Saves persistent user settings (tool UI + guide/render settings).
	bool save(const Scene& scene, const float viewportBg[3], bool showControlsOverlay, float uiScale, int windowWidth, int windowHeight, bool windowMaximized);

	// For debugging / UI (optional): absolute path to the settings file.
	std::string settingsPath();
}
