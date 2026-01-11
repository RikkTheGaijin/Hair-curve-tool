#include "GpuSolver.h"

#include "Scene.h"
#include "Physics.h"

#if defined(HAIRTOOL_ENABLE_CUDA)
#include "cuda/CudaHairSolver.h"
#endif

namespace {
#if defined(HAIRTOOL_ENABLE_CUDA)
	CudaHairSolver g_solver;
	bool g_inited = false;
#endif
}

bool GpuSolver::isAvailable() {
#if defined(HAIRTOOL_ENABLE_CUDA)
	return CudaHairSolver::isCudaRuntimeAvailable();
#else
	return false;
#endif
}

void GpuSolver::step(Scene& scene, float dt) {
#if defined(HAIRTOOL_ENABLE_CUDA)
	if (!CudaHairSolver::isCudaRuntimeAvailable()) {
		Physics::step(scene, dt);
		return;
	}
	if (!g_inited) {
		g_solver.init();
		g_inited = true;
	}
	g_solver.step(scene, dt);
#else
	Physics::step(scene, dt);
#endif
}
