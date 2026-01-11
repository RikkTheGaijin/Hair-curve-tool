#include "ImageLoader.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

#include <memory>

static std::wstring toWide(const std::string& s) {
	if (s.empty()) return std::wstring();
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	if (len <= 0) {
		// Fallback: try ANSI
		len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
		if (len <= 0) return std::wstring();
		std::wstring w((size_t)len, L'\0');
		MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), len);
		if (!w.empty() && w.back() == L'\0') w.pop_back();
		return w;
	}
	std::wstring w((size_t)len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
	if (!w.empty() && w.back() == L'\0') w.pop_back();
	return w;
}

template <typename T>
struct ComReleaser {
	void operator()(T* p) const { if (p) p->Release(); }
};

template <typename T>
using ComPtr = std::unique_ptr<T, ComReleaser<T>>;

bool ImageLoader::loadRGBA8(const std::string& path, int& outW, int& outH, std::vector<unsigned char>& outPixels) {
	outW = 0;
	outH = 0;
	outPixels.clear();

	static bool comInit = false;
	if (!comInit) {
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		(void)hr;
		comInit = true;
	}

	ComPtr<IWICImagingFactory> factory;
	{
		IWICImagingFactory* raw = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
		if (FAILED(hr) || !raw) return false;
		factory.reset(raw);
	}

	std::wstring wpath = toWide(path);
	if (wpath.empty()) return false;

	ComPtr<IWICBitmapDecoder> decoder;
	{
		IWICBitmapDecoder* raw = nullptr;
		HRESULT hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &raw);
		if (FAILED(hr) || !raw) return false;
		decoder.reset(raw);
	}

	ComPtr<IWICBitmapFrameDecode> frame;
	{
		IWICBitmapFrameDecode* raw = nullptr;
		HRESULT hr = decoder->GetFrame(0, &raw);
		if (FAILED(hr) || !raw) return false;
		frame.reset(raw);
	}

	UINT w = 0, h = 0;
	if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) return false;

	ComPtr<IWICFormatConverter> converter;
	{
		IWICFormatConverter* raw = nullptr;
		HRESULT hr = factory->CreateFormatConverter(&raw);
		if (FAILED(hr) || !raw) return false;
		converter.reset(raw);
	}

	// Convert to RGBA8
	{
		HRESULT hr = converter->Initialize(
			frame.get(),
			GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0,
			WICBitmapPaletteTypeCustom);
		if (FAILED(hr)) return false;
	}

	outW = (int)w;
	outH = (int)h;
	outPixels.resize((size_t)outW * (size_t)outH * 4u);
	const UINT stride = (UINT)outW * 4u;
	const UINT bufSize = stride * (UINT)outH;

	if (FAILED(converter->CopyPixels(nullptr, stride, bufSize, outPixels.data()))) {
		outPixels.clear();
		outW = outH = 0;
		return false;
	}

	return true;
}

#else

bool ImageLoader::loadRGBA8(const std::string&, int& outW, int& outH, std::vector<unsigned char>& outPixels) {
	outW = 0;
	outH = 0;
	outPixels.clear();
	return false;
}

#endif
