#include "ImportPly.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace {
	static std::string trim(const std::string& s) {
		size_t a = 0;
		while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
		size_t b = s.size();
		while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
		return s.substr(a, b - a);
	}

	static bool startsWith(const std::string& s, const char* pfx) {
		return s.rfind(pfx, 0) == 0;
	}
}

static bool parseLayerComment(const std::string& line, ImportPly::ImportedLayer& outLayer) {
	const std::string prefix = "comment layer ";
	if (line.rfind(prefix, 0) != 0) return false;
	std::string rest = line.substr(prefix.size());
	std::istringstream iss(rest);
	int id = 0;
	if (!(iss >> id)) return false;

	std::string name;
	iss >> std::ws;
	char c = (char)iss.peek();
	if (c == '"') {
		iss.get();
		std::getline(iss, name, '"');
	} else {
		iss >> name;
	}

	float r = 0.90f, g = 0.75f, b = 0.22f;
	int vis = 1;
	iss >> r >> g >> b >> vis;

	outLayer.id = id;
	outLayer.name = name.empty() ? (std::string("Layer ") + std::to_string(id)) : name;
	outLayer.color = glm::vec3(r, g, b);
	outLayer.visible = (vis != 0);
	return true;
}

bool ImportPly::loadCurves(const std::string& path, std::vector<ImportedCurve>& outCurves, std::vector<ImportedLayer>* outLayers, bool* outHasLayerInfo, std::string* outError) {
	outCurves.clear();
	if (outLayers) outLayers->clear();
	if (outHasLayerInfo) *outHasLayerInfo = false;
	if (outError) outError->clear();

	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) {
		if (outError) *outError = "Failed to open file";
		return false;
	}

	std::string line;
	bool sawPly = false;
	bool ascii = false;
	bool inVertex = false;
	int vertexCount = 0;
	std::vector<std::string> props;

	std::map<int, ImportedLayer> layersById;
	while (std::getline(f, line)) {
		line = trim(line);
		if (line.empty()) continue;

		if (!sawPly) {
			sawPly = (line == "ply");
			if (!sawPly) {
				if (outError) *outError = "Not a PLY file";
				return false;
			}
			continue;
		}

		if (startsWith(line, "format ")) {
			// format ascii 1.0
			ascii = (line.find("ascii") != std::string::npos);
			continue;
		}

		if (startsWith(line, "comment layer ")) {
			ImportedLayer layer;
			if (parseLayerComment(line, layer)) {
				layersById[layer.id] = layer;
			}
			continue;
		}

		if (startsWith(line, "element vertex ")) {
			std::istringstream iss(line);
			std::string a, b;
			iss >> a >> b >> vertexCount;
			inVertex = true;
			props.clear();
			continue;
		}

		if (startsWith(line, "element ")) {
			inVertex = false;
			continue;
		}

		if (startsWith(line, "property ") && inVertex) {
			// property float x
			std::istringstream iss(line);
			std::string kw, type, name;
			iss >> kw >> type >> name;
			if (!name.empty()) props.push_back(name);
			continue;
		}

		if (line == "end_header") break;
	}

	if (!ascii) {
		if (outError) *outError = "Only ASCII PLY is supported";
		return false;
	}
	if (vertexCount <= 0) {
		if (outError) *outError = "PLY has no vertices";
		return false;
	}

	auto findProp = [&](const char* name) -> int {
		auto it = std::find(props.begin(), props.end(), std::string(name));
		if (it == props.end()) return -1;
		return (int)std::distance(props.begin(), it);
	};

	const int ix = findProp("x");
	const int iy = findProp("y");
	const int iz = findProp("z");
	if (ix < 0 || iy < 0 || iz < 0) {
		if (outError) *outError = "PLY is missing x/y/z properties";
		return false;
	}
	const int iAnchor = findProp("anchor");
	const int iCurveId = findProp("curve_id");
	const int iLayerId = findProp("layer_id");
	bool sawNonZeroLayerId = false;

	// Grouping: prefer curve_id if present. Otherwise, split by anchor==1 if present; else treat as one curve.
	std::map<int, ImportedCurve> curvesById;
	ImportedCurve current;
	bool useAnchorSplitting = (iCurveId < 0 && iAnchor >= 0);

	for (int v = 0; v < vertexCount; v++) {
		if (!std::getline(f, line)) break;
		line = trim(line);
		if (line.empty()) {
			v--;
			continue;
		}

		std::istringstream iss(line);
		std::vector<std::string> toks;
		std::string tok;
		while (iss >> tok) toks.push_back(tok);
		if ((int)toks.size() < 3) continue;

		auto getFloat = [&](int idx) -> float {
			if (idx < 0 || idx >= (int)toks.size()) return 0.0f;
			return (float)std::atof(toks[(size_t)idx].c_str());
		};
		auto getInt = [&](int idx) -> int {
			if (idx < 0 || idx >= (int)toks.size()) return 0;
			return std::atoi(toks[(size_t)idx].c_str());
		};

		glm::vec3 p(getFloat(ix), getFloat(iy), getFloat(iz));
		int anchor = (iAnchor >= 0) ? getInt(iAnchor) : 0;
		int cid = (iCurveId >= 0) ? getInt(iCurveId) : 0;
		int lid = (iLayerId >= 0) ? getInt(iLayerId) : 0;
		if (lid != 0) sawNonZeroLayerId = true;

		if (iCurveId >= 0) {
			ImportedCurve& c = curvesById[cid];
			int localIndex = (int)c.points.size();
			c.points.push_back(p);
			if (c.layerId == 0 && lid != 0) c.layerId = lid;
			if (anchor == 1 && c.anchorIndex < 0) c.anchorIndex = localIndex;
		} else if (useAnchorSplitting) {
			if (anchor == 1 && !current.points.empty()) {
				outCurves.push_back(current);
				current = ImportedCurve();
			}
			int localIndex = (int)current.points.size();
			current.points.push_back(p);
			if (current.layerId == 0 && lid != 0) current.layerId = lid;
			if (anchor == 1 && current.anchorIndex < 0) current.anchorIndex = localIndex;
		} else {
			int localIndex = (int)current.points.size();
			current.points.push_back(p);
			if (current.layerId == 0 && lid != 0) current.layerId = lid;
			if (anchor == 1 && current.anchorIndex < 0) current.anchorIndex = localIndex;
		}
	}

	if (iCurveId >= 0) {
		for (auto& kv : curvesById) {
			if (kv.second.points.size() >= 2) outCurves.push_back(kv.second);
		}
	} else {
		if (current.points.size() >= 2) outCurves.push_back(current);
	}

	if (outHasLayerInfo) {
		*outHasLayerInfo = (!layersById.empty()) || sawNonZeroLayerId;
	}

	if (outLayers && !layersById.empty()) {
		for (const auto& kv : layersById) {
			outLayers->push_back(kv.second);
		}
	}

	if (outCurves.empty()) {
		if (outError) *outError = "No curves found";
		return false;
	}

	return true;
}
