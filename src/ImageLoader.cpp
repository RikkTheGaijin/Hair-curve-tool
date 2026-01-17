#include "ImageLoader.h"
#include "Log.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <jpeglib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <setjmp.h>
#include <algorithm>

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

static bool loadWithStb(const std::string& path, int& outW, int& outH, std::vector<unsigned char>& outPixels) {
	int w = 0, h = 0, comp = 0;
	stbi_uc* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
	if (!data || w <= 0 || h <= 0) {
		HT_ERR("ImageLoader: stb_image failed for %s (%s)\n", path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "unknown");
		if (data) stbi_image_free(data);
		return false;
	}
	outW = w;
	outH = h;
	outPixels.assign(data, data + (size_t)w * (size_t)h * 4u);
	stbi_image_free(data);

	// Convert RGBA -> BGRA to match WIC output and GL upload format.
	for (size_t i = 0; i + 3 < outPixels.size(); i += 4) {
		std::swap(outPixels[i], outPixels[i + 2]);
	}

	return true;
}

struct JpegErrorManager {
	jpeg_error_mgr pub;
	jmp_buf setjmpBuffer;
};

static void jpegErrorExit(j_common_ptr cinfo) {
	JpegErrorManager* err = reinterpret_cast<JpegErrorManager*>(cinfo->err);
	(*cinfo->err->output_message)(cinfo);
	longjmp(err->setjmpBuffer, 1);
}

static bool hasJpegExtension(const std::string& path) {
	std::string lower;
	lower.reserve(path.size());
	for (char c : path) lower.push_back((char)std::tolower((unsigned char)c));
	return lower.ends_with(".jpg") || lower.ends_with(".jpeg");
}

static bool hasJpegSignature(const std::string& path) {
	FILE* f = nullptr;
	fopen_s(&f, path.c_str(), "rb");
	if (!f) return false;
	unsigned char sig[2] = {0, 0};
	size_t r = std::fread(sig, 1, 2, f);
	std::fclose(f);
	return r == 2 && sig[0] == 0xFF && sig[1] == 0xD8;
}

static bool loadWithJpeg(const std::string& path, int& outW, int& outH, std::vector<unsigned char>& outPixels) {
	FILE* f = nullptr;
	fopen_s(&f, path.c_str(), "rb");
	if (!f) {
		HT_ERR("ImageLoader: jpeg fopen failed for %s\n", path.c_str());
		return false;
	}

	jpeg_decompress_struct cinfo;
	JpegErrorManager jerr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpegErrorExit;
	if (setjmp(jerr.setjmpBuffer)) {
		jpeg_destroy_decompress(&cinfo);
		std::fclose(f);
		HT_ERR("ImageLoader: jpeg decode failed for %s\n", path.c_str());
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, f);
	jpeg_read_header(&cinfo, TRUE);
	// Request RGB output (handles CMYK/YCCK where supported)
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	const int w = (int)cinfo.output_width;
	const int h = (int)cinfo.output_height;
	const int comps = (int)cinfo.output_components;
	if (w <= 0 || h <= 0 || (comps != 3 && comps != 1)) {
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		std::fclose(f);
		HT_ERR("ImageLoader: jpeg invalid output for %s\n", path.c_str());
		return false;
	}

	outW = w;
	outH = h;
	outPixels.resize((size_t)w * (size_t)h * 4u);

	const size_t rowStride = (size_t)w * (size_t)comps;
	std::vector<unsigned char> row(rowStride);
	JSAMPROW rowPtr = row.data();

	for (int y = 0; y < h; ++y) {
		jpeg_read_scanlines(&cinfo, &rowPtr, 1);
		unsigned char* dst = outPixels.data() + (size_t)y * (size_t)w * 4u;
		if (comps == 3) {
			for (int x = 0; x < w; ++x) {
				unsigned char r = row[(size_t)x * 3u + 0u];
				unsigned char g = row[(size_t)x * 3u + 1u];
				unsigned char b = row[(size_t)x * 3u + 2u];
				dst[(size_t)x * 4u + 0u] = b;
				dst[(size_t)x * 4u + 1u] = g;
				dst[(size_t)x * 4u + 2u] = r;
				dst[(size_t)x * 4u + 3u] = 255;
			}
		} else {
			for (int x = 0; x < w; ++x) {
				unsigned char v = row[(size_t)x];
				dst[(size_t)x * 4u + 0u] = v;
				dst[(size_t)x * 4u + 1u] = v;
				dst[(size_t)x * 4u + 2u] = v;
				dst[(size_t)x * 4u + 3u] = 255;
			}
		}
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	std::fclose(f);
	return true;
}

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
		if (FAILED(hr) || !raw) {
			HT_ERR("ImageLoader: CoCreateInstance(CLSID_WICImagingFactory) failed (0x%08X) for %s\n", (unsigned)hr, path.c_str());
			return false;
		}
		factory.reset(raw);
	}

	std::wstring wpath = toWide(path);
	if (wpath.empty()) {
		HT_ERR("ImageLoader: toWide failed for %s\n", path.c_str());
		return false;
	}

	ComPtr<IWICBitmapDecoder> decoder;
	{
		IWICBitmapDecoder* raw = nullptr;
		HRESULT hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &raw);
		if (FAILED(hr) || !raw) {
			HT_ERR("ImageLoader: CreateDecoderFromFilename failed (0x%08X) for %s; trying stb_image\n", (unsigned)hr, path.c_str());
			if (loadWithStb(path, outW, outH, outPixels)) return true;
			if (hasJpegExtension(path) || hasJpegSignature(path)) {
				HT_ERR("ImageLoader: stb_image failed; trying libjpeg for %s\n", path.c_str());
				return loadWithJpeg(path, outW, outH, outPixels);
			}
			return false;
		}
		decoder.reset(raw);
	}

	ComPtr<IWICBitmapFrameDecode> frame;
	{
		IWICBitmapFrameDecode* raw = nullptr;
		HRESULT hr = decoder->GetFrame(0, &raw);
		if (FAILED(hr) || !raw) {
			HT_ERR("ImageLoader: GetFrame failed (0x%08X) for %s\n", (unsigned)hr, path.c_str());
			return false;
		}
		frame.reset(raw);
	}

	UINT w = 0, h = 0;
	if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
		HT_ERR("ImageLoader: GetSize failed for %s\n", path.c_str());
		return false;
	}

	IWICBitmapSource* source = frame.get();
	ComPtr<IWICFormatConverter> converter;
	GUID srcFmt = GUID_WICPixelFormatUndefined;
	if (FAILED(frame->GetPixelFormat(&srcFmt))) {
		HT_ERR("ImageLoader: GetPixelFormat failed for %s\n", path.c_str());
		return false;
	}

	if (!IsEqualGUID(srcFmt, GUID_WICPixelFormat32bppBGRA)) {
		IWICFormatConverter* raw = nullptr;
		HRESULT hr = factory->CreateFormatConverter(&raw);
		if (FAILED(hr) || !raw) {
			HT_ERR("ImageLoader: CreateFormatConverter failed (0x%08X) for %s\n", (unsigned)hr, path.c_str());
			return false;
		}
		converter.reset(raw);
		BOOL canConvert = FALSE;
		hr = converter->CanConvert(srcFmt, GUID_WICPixelFormat32bppBGRA, &canConvert);
		if (FAILED(hr) || !canConvert) {
			HT_ERR("ImageLoader: Unsupported pixel format for %s\n", path.c_str());
			return false;
		}
		hr = converter->Initialize(
			frame.get(),
			GUID_WICPixelFormat32bppBGRA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0,
			WICBitmapPaletteTypeCustom);
		if (FAILED(hr)) {
			HT_ERR("ImageLoader: Converter Initialize failed (0x%08X) for %s\n", (unsigned)hr, path.c_str());
			return false;
		}
		source = converter.get();
	}

	outW = (int)w;
	outH = (int)h;
	outPixels.resize((size_t)outW * (size_t)outH * 4u);
	const UINT stride = (UINT)outW * 4u;
	const UINT bufSize = stride * (UINT)outH;

	if (FAILED(source->CopyPixels(nullptr, stride, bufSize, outPixels.data()))) {
		HT_ERR("ImageLoader: CopyPixels failed for %s\n", path.c_str());
		outPixels.clear();
		outW = outH = 0;
		return false;
	}

	return true;
}

#else

bool ImageLoader::loadRGBA8(const std::string& path, int& outW, int& outH, std::vector<unsigned char>& outPixels) {
	outW = 0;
	outH = 0;
	outPixels.clear();
	if (loadWithStb(path, outW, outH, outPixels)) return true;
	if (hasJpegExtension(path) || hasJpegSignature(path)) {
		return loadWithJpeg(path, outW, outH, outPixels);
	}
	return false;
}

#endif
