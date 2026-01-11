#pragma once

#include "Camera.h"

class MayaCameraController : public Camera {
public:
	void handleMouse(bool alt, bool lmb, bool mmb, bool rmb, float dx, float dy, float wheel);
};
