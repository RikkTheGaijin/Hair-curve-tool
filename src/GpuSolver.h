#pragma once

#include <cstddef>

class Scene;

namespace GpuSolver {
	// Returns true if the CUDA backend is compiled in and a device is available.
	bool isAvailable();

	// Step the simulation using the GPU backend. Falls back to CPU if unavailable.
	void step(Scene& scene, float dt);
}
