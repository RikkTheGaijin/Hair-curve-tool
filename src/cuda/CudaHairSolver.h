#pragma once

#include <cstdint>
#include <vector>

class Scene;

// CUDA-backed hair guide solver.
// This is intentionally small and hair-focused (not YarnBall). It uses the same *kind* of GPU constraint-iteration approach.
class CudaHairSolver {
public:
	static bool isCudaRuntimeAvailable();

	void init();
	void step(Scene& scene, float dt);

private:
	bool m_ready = false;
	uint64_t m_meshVersion = 0;

	// Packed buffers for all curves
	std::vector<float> m_h_pos;  // xyzxyz...
	std::vector<float> m_h_prev;
	std::vector<unsigned char> m_h_pinned; // per particle, 1 if pinned
	std::vector<int> m_h_curveOffsets;  // per curve start index (particle index)
	std::vector<int> m_h_curveCounts;   // per curve particle count
	std::vector<float> m_h_restLen;     // per curve segment rest length
	std::vector<int> m_h_pinnedRoot;    // per curve, root pinned (1)

	// Device pointers (opaque in header)
	void* m_d_pos = nullptr;
	void* m_d_prev = nullptr;
	void* m_d_curveOffsets = nullptr;
	void* m_d_curveCounts = nullptr;
	void* m_d_restLen = nullptr;
	void* m_d_pinned = nullptr;
	void* m_d_fieldCp = nullptr;
	void* m_d_fieldN = nullptr;
	int m_fieldRes = 0;
	float m_fieldVoxel = 0.0f;
	float m_fieldOrigin[3]{0,0,0};

	int m_totalParticles = 0;
	int m_curveCount = 0;

	void ensureCapacity(int totalParticles, int curveCount);
	void freeDevice();
	void ensureFieldUploaded(const Scene& scene);
};
