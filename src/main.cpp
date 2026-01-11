#include "App.h"
#include "MayaCameraController.h"
#include "Scene.h"
#include "Renderer.h"

int main() {
	App app;
	if (!app.init()) return 1;
	app.run();
	app.shutdown();
	return 0;
}
