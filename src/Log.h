#pragma once

// Minimal logging macros.
// Enable verbose logging by defining HAIRTOOL_VERBOSE_LOGGING=1 at compile time.

#include <cstdio>

#if defined(HAIRTOOL_VERBOSE_LOGGING) && (HAIRTOOL_VERBOSE_LOGGING)
	#define HT_LOG(...)  std::printf(__VA_ARGS__)
	#define HT_WARN(...) std::printf(__VA_ARGS__)
#else
	#define HT_LOG(...)  ((void)0)
	#define HT_WARN(...) ((void)0)
#endif

// Errors are rare; keep them always.
#define HT_ERR(...) std::fprintf(stderr, __VA_ARGS__)
