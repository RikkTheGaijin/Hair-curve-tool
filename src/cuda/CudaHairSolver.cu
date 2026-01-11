#include "CudaHairSolver.h"

#include "Scene.h"
#include "HairGuides.h"
#include "Mesh.h"
#include "Bvh.h"

#include "MeshDistanceField.h"
#include "Physics.h"

#include <cuda_runtime.h>

#include <cstdio>
#include <cmath>
#include <algorithm>

#include <glm/glm.hpp>

static inline void checkCuda(cudaError_t err, const char* msg) {
	if (err == cudaSuccess) return;
	std::fprintf(stderr, "[CUDA] %s: %s\n", msg, cudaGetErrorString(err));
}

static inline void checkCudaKernel(const char* msg) {
	cudaError_t err = cudaGetLastError();
	if (err == cudaSuccess) return;
	std::fprintf(stderr, "[CUDA] %s: %s\n", msg, cudaGetErrorString(err));
}

bool CudaHairSolver::isCudaRuntimeAvailable() {
	int count = 0;
	cudaError_t err = cudaGetDeviceCount(&count);
	return (err == cudaSuccess) && (count > 0);
}

void CudaHairSolver::init() {
	if (m_ready) return;
	cudaSetDevice(0);
	cudaDeviceProp prop{};
	cudaGetDeviceProperties(&prop, 0);
	std::fprintf(stderr, "[CUDA] Using device: %s (sm_%d%d)\n", prop.name, prop.major, prop.minor);
	m_ready = true;
}

void CudaHairSolver::freeDevice() {
	if (m_d_pos) cudaFree(m_d_pos);
	if (m_d_prev) cudaFree(m_d_prev);
	if (m_d_curveOffsets) cudaFree(m_d_curveOffsets);
	if (m_d_curveCounts) cudaFree(m_d_curveCounts);
	if (m_d_restLen) cudaFree(m_d_restLen);
	if (m_d_pinned) cudaFree(m_d_pinned);
	if (m_d_fieldCp) cudaFree(m_d_fieldCp);
	if (m_d_fieldN) cudaFree(m_d_fieldN);
	m_d_pos = m_d_prev = m_d_curveOffsets = m_d_curveCounts = m_d_restLen = nullptr;
	m_d_pinned = nullptr;
	m_d_fieldCp = m_d_fieldN = nullptr;
}

void CudaHairSolver::ensureCapacity(int totalParticles, int curveCount) {
	if (totalParticles <= m_totalParticles && curveCount <= m_curveCount && m_d_pos) return;

	freeDevice();
	m_totalParticles = totalParticles;
	m_curveCount = curveCount;

	size_t posBytes = (size_t)m_totalParticles * 3 * sizeof(float);
	checkCuda(cudaMalloc(&m_d_pos, posBytes), "cudaMalloc pos");
	checkCuda(cudaMalloc(&m_d_prev, posBytes), "cudaMalloc prev");
	checkCuda(cudaMalloc(&m_d_curveOffsets, (size_t)m_curveCount * sizeof(int)), "cudaMalloc curveOffsets");
	checkCuda(cudaMalloc(&m_d_curveCounts, (size_t)m_curveCount * sizeof(int)), "cudaMalloc curveCounts");
	checkCuda(cudaMalloc(&m_d_restLen, (size_t)m_curveCount * sizeof(float)), "cudaMalloc restLen");
	checkCuda(cudaMalloc(&m_d_pinned, (size_t)m_totalParticles * sizeof(unsigned char)), "cudaMalloc pinned");
}

void CudaHairSolver::ensureFieldUploaded(const Scene& scene) {
	if (!scene.mesh()) return;
	MeshDistanceField& field = const_cast<MeshDistanceField&>(scene.meshDistanceField());
	// Build field lazily on first GPU solver use
	if (!field.valid()) {
		field.build(*scene.mesh(), 96, 0.03f);
	}
	if (!field.valid()) return;
	if (m_meshVersion == scene.meshVersion() && m_d_fieldCp && m_d_fieldN) return;

	if (m_d_fieldCp) cudaFree(m_d_fieldCp);
	if (m_d_fieldN) cudaFree(m_d_fieldN);
	m_d_fieldCp = m_d_fieldN = nullptr;

	m_fieldRes = field.resolution();
	m_fieldVoxel = field.voxelSize();
	glm::vec3 org = field.origin();
	m_fieldOrigin[0] = org.x; m_fieldOrigin[1] = org.y; m_fieldOrigin[2] = org.z;

	size_t count = (size_t)m_fieldRes * (size_t)m_fieldRes * (size_t)m_fieldRes;
	checkCuda(cudaMalloc(&m_d_fieldCp, count * sizeof(float4)), "cudaMalloc field cp");
	checkCuda(cudaMalloc(&m_d_fieldN, count * sizeof(float4)), "cudaMalloc field n");
	checkCuda(cudaMemcpy(m_d_fieldCp, field.closestPoints().data(), count * sizeof(float4), cudaMemcpyHostToDevice), "H2D field cp");
	checkCuda(cudaMemcpy(m_d_fieldN, field.normals().data(), count * sizeof(float4), cudaMemcpyHostToDevice), "H2D field n");

	m_meshVersion = scene.meshVersion();
}

__device__ float3 f3_add(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
__device__ float3 f3_sub(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
__device__ float3 f3_mul(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
__device__ float f3_dot(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
__device__ float f3_len(float3 a) { return sqrtf(f3_dot(a, a)); }
__device__ float3 f3_norm(float3 a) { float l = f3_len(a); return (l > 1e-8f) ? f3_mul(a, 1.0f / l) : make_float3(0,1,0); }

__global__ void integrateKernel(float* pos, float* prev, const unsigned char* pinned, int totalParticles, float dt, float gx, float gy, float gz) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= totalParticles) return;
	if (pinned && pinned[i]) return;

	float3 x = make_float3(pos[i*3+0], pos[i*3+1], pos[i*3+2]);
	float3 p = make_float3(prev[i*3+0], prev[i*3+1], prev[i*3+2]);

	// pinned root will be handled on host by overwriting positions
	float3 v = f3_sub(x, p);
	float dt2 = dt * dt;
	float3 a = make_float3(gx, gy, gz);
	float3 xNew = f3_add(f3_add(x, v), f3_mul(a, dt2));

	prev[i*3+0] = x.x; prev[i*3+1] = x.y; prev[i*3+2] = x.z;
	pos[i*3+0] = xNew.x; pos[i*3+1] = xNew.y; pos[i*3+2] = xNew.z;
}

__global__ void distanceConstraintsKernel(float* pos, const int* curveOffsets, const int* curveCounts, const float* restLen, int curveCount, int parity) {
	int cid = blockIdx.x;
	if (cid >= curveCount) return;
	int count = curveCounts[cid];
	int base = curveOffsets[cid];
	float rl = restLen[cid];

	int e = threadIdx.x + blockIdx.y * blockDim.x;
	int edgeCount = count - 1;
	int edge = e * 2 + parity;
	if (edge >= edgeCount) return;

	int i0 = base + edge;
	int i1 = base + edge + 1;
	// root is pinned
	bool pinned0 = (edge == 0);

	float3 p0 = make_float3(pos[i0*3+0], pos[i0*3+1], pos[i0*3+2]);
	float3 p1 = make_float3(pos[i1*3+0], pos[i1*3+1], pos[i1*3+2]);
	float3 d = f3_sub(p1, p0);
	float len = f3_len(d);
	if (len < 1e-8f) return;
	float3 n = f3_mul(d, 1.0f / len);
	float C = len - rl;

	float w0 = pinned0 ? 0.0f : 1.0f;
	float w1 = 1.0f;
	float wsum = w0 + w1;
	if (wsum <= 0.0f) return;
	float3 corr = f3_mul(n, C / wsum);

	p0 = f3_add(p0, f3_mul(corr, -w0));
	p1 = f3_add(p1, f3_mul(corr,  w1));

	pos[i0*3+0] = p0.x; pos[i0*3+1] = p0.y; pos[i0*3+2] = p0.z;
	pos[i1*3+0] = p1.x; pos[i1*3+1] = p1.y; pos[i1*3+2] = p1.z;
}

__global__ void dampingKernel(float* pos, float* prev, const unsigned char* pinned, int totalParticles, float damping) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= totalParticles) return;
	if (pinned && pinned[i]) return;
	float3 x = make_float3(pos[i*3+0], pos[i*3+1], pos[i*3+2]);
	float3 p = make_float3(prev[i*3+0], prev[i*3+1], prev[i*3+2]);
	float3 v = f3_sub(x, p);
	prev[i*3+0] = x.x - v.x * damping;
	prev[i*3+1] = x.y - v.y * damping;
	prev[i*3+2] = x.z - v.z * damping;
}

__device__ int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

__device__ int fieldIndex(int x, int y, int z, int res) {
	return x + res * (y + res * z);
}

__global__ void meshCollisionKernel(float* pos, const int* curveOffsets, const int* curveCounts,
	const float4* fieldCp, const float4* fieldN,
	int fieldRes, float voxelSize, float3 origin,
	int curveCount, float thickness) {
	int cid = blockIdx.x;
	if (cid >= curveCount) return;
	int count = curveCounts[cid];
	int base = curveOffsets[cid];

	int local = threadIdx.x + blockIdx.y * blockDim.x;
	if (local >= count) return;
	if (local == 0) return; // root pinned

	int pi = base + local;
	float3 p = make_float3(pos[pi*3+0], pos[pi*3+1], pos[pi*3+2]);

	float3 rel = f3_sub(p, origin);
	int xi = (int)floorf(rel.x / voxelSize);
	int yi = (int)floorf(rel.y / voxelSize);
	int zi = (int)floorf(rel.z / voxelSize);
	if (xi < 0 || yi < 0 || zi < 0 || xi >= fieldRes || yi >= fieldRes || zi >= fieldRes) return;
	int idx = fieldIndex(xi, yi, zi, fieldRes);

	float4 cp4 = fieldCp[idx];
	float4 n4 = fieldN[idx];
	float3 cp = make_float3(cp4.x, cp4.y, cp4.z);
	float3 n = make_float3(n4.x, n4.y, n4.z);

	float3 d = f3_sub(p, cp);
	float dist = f3_len(d);
	if (dist >= thickness) return;

	float3 push = (dist > 1e-8f) ? f3_mul(d, 1.0f / dist) : f3_norm(n);
	// If push points opposite of the normal, prefer the normal direction (heuristic for "inside")
	if (f3_dot(push, n) < 0.0f) push = f3_norm(n);

	float pen = thickness - dist;
	p = f3_add(p, f3_mul(push, pen));
	pos[pi*3+0] = p.x; pos[pi*3+1] = p.y; pos[pi*3+2] = p.z;
}

void CudaHairSolver::step(Scene& scene, float dt) {
	if (!m_ready) init();
	if (!scene.mesh()) return;
	ensureFieldUploaded(scene);

	HairGuideSet& guides = scene.guides();
	GuideSettings& gs = scene.guideSettings();

	// Keep roots attached
	guides.updatePinnedRootsFromMesh(*scene.mesh());

	// Pack only selected curves (freeze unselected curves)
	std::vector<int> curveMap;
	curveMap.reserve(guides.curveCount());
	for (size_t i = 0; i < guides.curveCount(); i++) {
		if (guides.isCurveSelected(i)) curveMap.push_back((int)i);
	}

	m_curveCount = (int)curveMap.size();
	m_h_curveOffsets.resize((size_t)m_curveCount);
	m_h_curveCounts.resize((size_t)m_curveCount);
	m_h_restLen.resize((size_t)m_curveCount);

	int totalParticles = 0;
	for (int c = 0; c < m_curveCount; c++) {
		const HairCurve& hc = guides.curve((size_t)curveMap[(size_t)c]);
		m_h_curveOffsets[(size_t)c] = totalParticles;
		m_h_curveCounts[(size_t)c] = (int)hc.points.size();
		m_h_restLen[(size_t)c] = (hc.segmentRestLen > 0.0f) ? hc.segmentRestLen : (gs.defaultLength / (float)(glm::max(2, (int)hc.points.size()) - 1));
		totalParticles += (int)hc.points.size();
	}

	ensureCapacity(totalParticles, m_curveCount);
	m_h_pos.resize((size_t)totalParticles * 3);
	m_h_prev.resize((size_t)totalParticles * 3);
	m_h_pinned.assign((size_t)totalParticles, (unsigned char)0);

	for (int c = 0; c < m_curveCount; c++) {
		const HairCurve& hc = guides.curve((size_t)curveMap[(size_t)c]);
		int base = m_h_curveOffsets[(size_t)c];
		for (int i = 0; i < (int)hc.points.size(); i++) {
			glm::vec3 p = hc.points[(size_t)i];
			glm::vec3 pp = hc.prevPoints[(size_t)i];
			m_h_pos[(size_t)(base + i) * 3 + 0] = p.x;
			m_h_pos[(size_t)(base + i) * 3 + 1] = p.y;
			m_h_pos[(size_t)(base + i) * 3 + 2] = p.z;
			m_h_prev[(size_t)(base + i) * 3 + 0] = pp.x;
			m_h_prev[(size_t)(base + i) * 3 + 1] = pp.y;
			m_h_prev[(size_t)(base + i) * 3 + 2] = pp.z;
			if (i == 0) m_h_pinned[(size_t)(base + i)] = (unsigned char)1;
		}
	}

	checkCuda(cudaMemcpy(m_d_pos, m_h_pos.data(), m_h_pos.size() * sizeof(float), cudaMemcpyHostToDevice), "H2D pos");
	checkCuda(cudaMemcpy(m_d_prev, m_h_prev.data(), m_h_prev.size() * sizeof(float), cudaMemcpyHostToDevice), "H2D prev");
	checkCuda(cudaMemcpy(m_d_curveOffsets, m_h_curveOffsets.data(), m_h_curveOffsets.size() * sizeof(int), cudaMemcpyHostToDevice), "H2D offsets");
	checkCuda(cudaMemcpy(m_d_curveCounts, m_h_curveCounts.data(), m_h_curveCounts.size() * sizeof(int), cudaMemcpyHostToDevice), "H2D counts");
	checkCuda(cudaMemcpy(m_d_restLen, m_h_restLen.data(), m_h_restLen.size() * sizeof(float), cudaMemcpyHostToDevice), "H2D restLen");
	checkCuda(cudaMemcpy(m_d_pinned, m_h_pinned.data(), m_h_pinned.size() * sizeof(unsigned char), cudaMemcpyHostToDevice), "H2D pinned");

	// Roots are marked as pinned; integrate/damping kernels will skip them.

	// GPU integration
	float g = gs.gravity;
	if (scene.gravityOverrideHeld()) {
		// GPU solver uses one gravity value for all simulated curves.
		g = scene.gravityOverrideValue();
	}
	g = fmaxf(0.0f, g);
	int threads = 256;
	int blocks = (totalParticles + threads - 1) / threads;
	integrateKernel<<<blocks, threads>>>((float*)m_d_pos, (float*)m_d_prev, (const unsigned char*)m_d_pinned, totalParticles, dt, 0.0f, -g, 0.0f);
	checkCudaKernel("integrateKernel");

	int iters = std::clamp(gs.solverIterations, 1, 64);
	for (int it = 0; it < iters; it++) {
		// Even edges then odd edges (Gauss-Seidel-ish but parallel)
		dim3 grid((unsigned)m_curveCount, 64, 1);
		distanceConstraintsKernel<<<grid, 128>>>((float*)m_d_pos, (int*)m_d_curveOffsets, (int*)m_d_curveCounts, (float*)m_d_restLen, m_curveCount, 0);
		checkCudaKernel("distanceConstraintsKernel even");
		distanceConstraintsKernel<<<grid, 128>>>((float*)m_d_pos, (int*)m_d_curveOffsets, (int*)m_d_curveCounts, (float*)m_d_restLen, m_curveCount, 1);
		checkCudaKernel("distanceConstraintsKernel odd");

		if (gs.enableMeshCollision && m_d_fieldCp && m_d_fieldN) {
			float thickness = fmaxf(1e-6f, gs.collisionThickness);
			float3 org = make_float3(m_fieldOrigin[0], m_fieldOrigin[1], m_fieldOrigin[2]);
			meshCollisionKernel<<<grid, 128>>>((float*)m_d_pos, (int*)m_d_curveOffsets, (int*)m_d_curveCounts,
				(const float4*)m_d_fieldCp, (const float4*)m_d_fieldN,
				m_fieldRes, m_fieldVoxel, org,
				m_curveCount, thickness);
			checkCudaKernel("meshCollisionKernel");
		}
	}

	// Damping consistent with CPU solver (retain ~98% of velocity per step)
	dampingKernel<<<blocks, threads>>>((float*)m_d_pos, (float*)m_d_prev, (const unsigned char*)m_d_pinned, totalParticles, 0.98f);
	checkCudaKernel("dampingKernel");
	checkCuda(cudaDeviceSynchronize(), "sync");

	// Download
	checkCuda(cudaMemcpy(m_h_pos.data(), m_d_pos, m_h_pos.size() * sizeof(float), cudaMemcpyDeviceToHost), "D2H pos");	
	checkCuda(cudaMemcpy(m_h_prev.data(), m_d_prev, m_h_prev.size() * sizeof(float), cudaMemcpyDeviceToHost), "D2H prev");

	// Apply to scene curves
	for (int c = 0; c < m_curveCount; c++) {
		HairCurve& hc = guides.curve((size_t)curveMap[(size_t)c]);
		int base = m_h_curveOffsets[(size_t)c];
		for (int i = 0; i < (int)hc.points.size(); i++) {
			hc.points[(size_t)i] = glm::vec3(
				m_h_pos[(size_t)(base + i) * 3 + 0],
				m_h_pos[(size_t)(base + i) * 3 + 1],
				m_h_pos[(size_t)(base + i) * 3 + 2]
			);
			hc.prevPoints[(size_t)i] = glm::vec3(
				m_h_prev[(size_t)(base + i) * 3 + 0],
				m_h_prev[(size_t)(base + i) * 3 + 1],
				m_h_prev[(size_t)(base + i) * 3 + 2]
			);
		}
	}

	// Optional curve-curve collision remains CPU for now (mesh collision is on GPU).
	Physics::applyCurveCurveCollision(scene);
}
