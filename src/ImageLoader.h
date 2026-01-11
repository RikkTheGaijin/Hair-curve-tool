#pragma once

#include <string>
#include <vector>

namespace ImageLoader {
	// Loads an image file (PNG/JPG) into 8-bit RGBA pixels.
	// Returns false on failure.
	bool loadRGBA8(const std::string& path, int& outW, int& outH, std::vector<unsigned char>& outPixels);
}
