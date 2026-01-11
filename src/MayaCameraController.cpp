#include "MayaCameraController.h"

#include <glm/gtc/constants.hpp>

void MayaCameraController::handleMouse(bool alt, bool lmb, bool mmb, bool rmb, float dx, float dy, float wheel) {
	if (wheel != 0.0f) {
		// Mouse wheel = dolly
		dolly(-wheel * 20.0f);
	}

	if (!alt) return;

	if (lmb && !mmb && !rmb) {
		orbit(-dx * 0.005f, -dy * 0.005f);
		return;
	}
	if (mmb) {
		pan(dx, dy);
		return;
	}
	if (rmb && !lmb) {
		dolly(dy);
		return;
	}
}
