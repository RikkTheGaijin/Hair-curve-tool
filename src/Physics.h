#pragma once

class Scene;

namespace Physics {
	// MVP: fixed-timestep step; runs XPBD-like constraints on CPU.
	// Future: swap implementation with YarnBall/CUDA or GL-compute backend.
	void step(Scene& scene, float dt);
	void applyCurveCurveCollision(Scene& scene);
}
