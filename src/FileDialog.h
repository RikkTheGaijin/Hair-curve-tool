#pragma once

#include <string>

namespace FileDialog {
	// filter string uses Win32 format: "Description\0*.ext\0...\0"
	bool openFile(std::string& outPath, const char* filter);
	bool saveFile(std::string& outPath, const char* filter);
}
