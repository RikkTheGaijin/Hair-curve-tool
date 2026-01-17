#include "Serialization.h"

#include "Scene.h"
#include "Mesh.h"
#include "Camera.h"

#include <json/json.h>

#include <fstream>

static Json::Value vec3ToJson(const glm::vec3& v) {
	Json::Value a(Json::arrayValue);
	a.append(v.x);
	a.append(v.y);
	a.append(v.z);
	return a;
}

static glm::vec3 jsonToVec3(const Json::Value& a) {
	if (!a.isArray() || a.size() != 3) return glm::vec3(0.0f);
	return glm::vec3(a[0].asFloat(), a[1].asFloat(), a[2].asFloat());
}

bool Serialization::saveScene(const Scene& scene, const Camera& camera, const std::string& path) {
	Json::Value root;
	root["version"] = 2;
	root["meshPath"] = scene.meshPath();
	root["meshTexturePath"] = scene.meshTexturePath();

	// Camera
	{
		Json::Value jc;
		jc["target"] = vec3ToJson(camera.target());
		jc["yaw"] = camera.yaw();
		jc["pitch"] = camera.pitch();
		jc["distance"] = camera.distance();
		root["camera"] = jc;
	}

	const GuideSettings& gs = scene.guideSettings();
	Json::Value jgs;
	jgs["defaultLength"] = gs.defaultLength;
	jgs["defaultSteps"] = gs.defaultSteps;
	jgs["mirrorMode"] = gs.mirrorMode;
	jgs["enableSimulation"] = gs.enableSimulation;
	jgs["enableMeshCollision"] = gs.enableMeshCollision;
	jgs["enableCurveCollision"] = gs.enableCurveCollision;
	jgs["enableGpuSolver"] = gs.enableGpuSolver;
	jgs["collisionThickness"] = gs.collisionThickness;
	jgs["collisionFriction"] = gs.collisionFriction;
	jgs["solverIterations"] = gs.solverIterations;
	jgs["gravity"] = gs.gravity;
	jgs["damping"] = gs.damping;
	jgs["stiffness"] = gs.stiffness;
	jgs["dragLerp"] = gs.dragLerp;
	root["guideSettings"] = jgs;

	// Layers
	Json::Value layers(Json::arrayValue);
	for (size_t li = 0; li < scene.layerCount(); li++) {
		const LayerInfo& layer = scene.layer(li);
		Json::Value jl;
		jl["name"] = layer.name;
		jl["color"] = vec3ToJson(layer.color);
		jl["visible"] = layer.visible;
		layers.append(jl);
	}
	root["layers"] = layers;
	root["activeLayer"] = scene.activeLayer();

	Json::Value curves(Json::arrayValue);
	for (size_t ci = 0; ci < scene.guides().curveCount(); ci++) {
		const HairCurve& c = scene.guides().curve(ci);
		Json::Value jc;
		jc["rootTri"] = c.root.triIndex;
		jc["rootBary"] = vec3ToJson(c.root.bary);
		jc["layer"] = c.layerId;

		Json::Value pts(Json::arrayValue);
		for (const glm::vec3& p : c.points) pts.append(vec3ToJson(p));
		jc["points"] = pts;

		curves.append(jc);
	}
	root["curves"] = curves;

	Json::StreamWriterBuilder wb;
	wb["indentation"] = "  ";
	std::unique_ptr<Json::StreamWriter> writer(wb.newStreamWriter());

	std::ofstream f(path, std::ios::binary);
	if (!f.is_open()) return false;
	writer->write(root, &f);
	return true;
}
bool Serialization::loadScene(Scene& scene, Camera* camera, const std::string& path, bool* outCameraRestored) {
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) return false;

	Json::CharReaderBuilder rb;
	Json::Value root;
	std::string errs;
	if (!Json::parseFromStream(rb, f, &root, &errs)) {
		return false;
	}

	std::string meshPath = root.get("meshPath", "").asString();
	std::string meshTexturePath = root.get("meshTexturePath", "").asString();
	if (!meshPath.empty()) {
		scene.loadMeshFromObj(meshPath);
	}
	if (!scene.mesh()) {
		// Can't restore curve roots without the mesh; keep the scene empty.
		scene.guides().clear();
		if (outCameraRestored) *outCameraRestored = false;
		return true;
	}

	scene.setMeshTexturePath(meshTexturePath);

	bool cameraRestored = false;
	if (camera) {
		Json::Value jc = root["camera"];
		if (jc.isObject()) {
			glm::vec3 target = jsonToVec3(jc["target"]);
			float yaw = jc.get("yaw", camera->yaw()).asFloat();
			float pitch = jc.get("pitch", camera->pitch()).asFloat();
			float dist = jc.get("distance", camera->distance()).asFloat();
			camera->setState(target, dist, yaw, pitch);
			cameraRestored = true;
		}
	}

	GuideSettings& gs = scene.guideSettings();
	Json::Value jgs = root["guideSettings"];
	if (jgs.isObject()) {
		gs.defaultLength = jgs.get("defaultLength", gs.defaultLength).asFloat();
		gs.defaultSteps = jgs.get("defaultSteps", gs.defaultSteps).asInt();
		gs.mirrorMode = jgs.get("mirrorMode", gs.mirrorMode).asBool();
		gs.enableSimulation = jgs.get("enableSimulation", gs.enableSimulation).asBool();
		gs.enableMeshCollision = jgs.get("enableMeshCollision", gs.enableMeshCollision).asBool();
		gs.enableCurveCollision = jgs.get("enableCurveCollision", gs.enableCurveCollision).asBool();
		gs.enableGpuSolver = jgs.get("enableGpuSolver", gs.enableGpuSolver).asBool();
		gs.collisionThickness = jgs.get("collisionThickness", gs.collisionThickness).asFloat();
		gs.collisionFriction = jgs.get("collisionFriction", gs.collisionFriction).asFloat();
		gs.solverIterations = jgs.get("solverIterations", gs.solverIterations).asInt();
		gs.gravity = jgs.get("gravity", gs.gravity).asFloat();
		gs.damping = jgs.get("damping", gs.damping).asFloat();
		gs.stiffness = jgs.get("stiffness", gs.stiffness).asFloat();
		gs.dragLerp = jgs.get("dragLerp", gs.dragLerp).asFloat();
	}

	// Layers
	std::vector<LayerInfo> layers;
	Json::Value jlayers = root["layers"];
	if (jlayers.isArray()) {
		for (Json::ArrayIndex li = 0; li < jlayers.size(); li++) {
			Json::Value jl = jlayers[li];
			LayerInfo l;
			l.name = jl.get("name", std::string("Layer ") + std::to_string((int)li)).asString();
			l.color = jsonToVec3(jl["color"]);
			if (!jl["color"].isArray() || jl["color"].size() != 3) {
				l.color = glm::vec3(0.90f, 0.75f, 0.22f);
			}
			l.visible = jl.get("visible", true).asBool();
			layers.push_back(l);
		}
	}
	int activeLayer = root.get("activeLayer", 0).asInt();
	if (!layers.empty()) {
		scene.setLayers(layers, activeLayer);
	} else {
		// Backward compatibility: default single layer
		std::vector<LayerInfo> fallback;
		LayerInfo base;
		base.name = "Layer 0";
		base.color = glm::vec3(0.90f, 0.75f, 0.22f);
		base.visible = true;
		fallback.push_back(base);
		scene.setLayers(fallback, 0);
	}

	scene.guides().clear();
	Json::Value curves = root["curves"];
	if (curves.isArray()) {
		for (Json::ArrayIndex i = 0; i < curves.size(); i++) {
			Json::Value jc = curves[i];
			HairCurve c;
			c.root.triIndex = jc.get("rootTri", -1).asInt();
			c.root.bary = jsonToVec3(jc["rootBary"]);
			c.layerId = jc.get("layer", 0).asInt();
			if (c.layerId < 0 || c.layerId >= (int)scene.layerCount()) c.layerId = 0;

			Json::Value pts = jc["points"];
			if (pts.isArray()) {
				c.points.reserve(pts.size());
				c.prevPoints.reserve(pts.size());
				for (Json::ArrayIndex pi = 0; pi < pts.size(); pi++) {
					glm::vec3 p = jsonToVec3(pts[pi]);
					c.points.push_back(p);
					c.prevPoints.push_back(p);
				}
			}
			if (c.points.size() >= 2) {
				float sum = 0.0f;
				for (size_t si = 0; si + 1 < c.points.size(); si++) sum += glm::length(c.points[si + 1] - c.points[si]);
				c.segmentRestLen = sum / (float)(c.points.size() - 1);
			}

			// Append via addCurveOnMesh (to preserve internal invariants), then overwrite with loaded data.
			const LayerInfo& layer = scene.layer((size_t)c.layerId);
			scene.guides().addCurveOnMesh(*scene.mesh(), c.root.triIndex, c.root.bary,
				c.points.empty() ? glm::vec3(0) : c.points[0],
				glm::vec3(0, 1, 0), gs, c.layerId, layer.color, layer.visible);
			HairCurve& dst = scene.guides().curve(scene.guides().curveCount() - 1);
			dst = c;
			dst.color = layer.color;
			dst.visible = layer.visible;
		}
	}

	if (outCameraRestored) *outCameraRestored = cameraRestored;
	return true;
}
