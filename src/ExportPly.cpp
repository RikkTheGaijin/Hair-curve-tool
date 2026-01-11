#include "ExportPly.h"

#include "Scene.h"
#include "HairGuides.h"

#include <fstream>
#include <vector>

static glm::vec3 evalCatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
	float t2 = t * t;
	float t3 = t2 * t;
	return 0.5f * ((2.0f * p1) +
		(-p0 + p2) * t +
		(2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
		(-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

bool ExportPly::exportCurvesAsPointCloud(const Scene& scene, const std::string& path) {
	struct V { glm::vec3 p; int cid; };
	std::vector<V> pts;
	pts.reserve(4096);

	const int samplesPerSeg = 8;
	for (size_t ci = 0; ci < scene.guides().curveCount(); ci++) {
		const HairCurve& c = scene.guides().curve(ci);
		if (c.points.size() < 2) continue;

		for (size_t i = 0; i + 1 < c.points.size(); i++) {
			glm::vec3 p0 = (i == 0) ? c.points[i] : c.points[i - 1];
			glm::vec3 p1 = c.points[i];
			glm::vec3 p2 = c.points[i + 1];
			glm::vec3 p3 = (i + 2 < c.points.size()) ? c.points[i + 2] : c.points[i + 1];

			for (int s = 0; s < samplesPerSeg; s++) {
				float t = (float)s / (float)samplesPerSeg;
				pts.push_back({evalCatmullRom(p0, p1, p2, p3, t), (int)ci});
			}
		}
		pts.push_back({c.points.back(), (int)ci});
	}

	std::ofstream f(path, std::ios::binary);
	if (!f.is_open()) return false;

	f << "ply\n";
	f << "format ascii 1.0\n";
	f << "element vertex " << pts.size() << "\n";
	f << "property float x\n";
	f << "property float y\n";
	f << "property float z\n";
	f << "property int curve_id\n";
	f << "end_header\n";
	for (const V& v : pts) {
		f << v.p.x << " " << v.p.y << " " << v.p.z << " " << v.cid << "\n";
	}
	return true;
}
