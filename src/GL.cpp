#include "GL.h"

#include <glad/glad.h>
#include <cstdio>

#ifdef HAIRTOOL_ENABLE_GL_DEBUG
static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*) {
	(void)source;
	(void)id;
	if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

	const char* typeStr = "OTHER";
	switch (type) {
		case GL_DEBUG_TYPE_ERROR: typeStr = "ERROR"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "DEPRECATED"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: typeStr = "UNDEFINED"; break;
		case GL_DEBUG_TYPE_PORTABILITY: typeStr = "PORTABILITY"; break;
		case GL_DEBUG_TYPE_PERFORMANCE: typeStr = "PERFORMANCE"; break;
		case GL_DEBUG_TYPE_MARKER: typeStr = "MARKER"; break;
		default: break;
	}

	const char* sevStr = "LOW";
	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: sevStr = "HIGH"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: sevStr = "MED"; break;
		case GL_DEBUG_SEVERITY_LOW: sevStr = "LOW"; break;
		default: break;
	}

	std::fprintf(stderr, "[GL %s/%s] %s\n", typeStr, sevStr, message);
}
#endif

void GL::enableDebugOutput() {
#ifdef HAIRTOOL_ENABLE_GL_DEBUG
	if (glDebugMessageCallback != nullptr) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(glDebugCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	}
#endif
}
