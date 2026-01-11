#include "FileDialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <vector>

static bool showFileDialog(bool save, std::string& outPath, const char* filter) {
	char filename[MAX_PATH] = {0};

	OPENFILENAMEA ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (save) {
		ofn.Flags |= OFN_OVERWRITEPROMPT;
	} else {
		ofn.Flags |= OFN_FILEMUSTEXIST;
	}

	BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
	if (!ok) return false;
	outPath = std::string(filename);
	return true;
}

bool FileDialog::openFile(std::string& outPath, const char* filter) {
	return showFileDialog(false, outPath, filter);
}

bool FileDialog::saveFile(std::string& outPath, const char* filter) {
	return showFileDialog(true, outPath, filter);
}

#else
#include <imgui.h>

bool FileDialog::openFile(std::string& outPath, const char*) {
	ImGui::OpenPopup("OpenFileNotSupported");
	outPath.clear();
	return false;
}

bool FileDialog::saveFile(std::string& outPath, const char*) {
	ImGui::OpenPopup("SaveFileNotSupported");
	outPath.clear();
	return false;
}
#endif
