#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(CYCOMM_SDK_BUILD)
#    define CYCOMM_SDK_API __declspec(dllexport)
#  else
#    define CYCOMM_SDK_API __declspec(dllimport)
#  endif
#else
#  define CYCOMM_SDK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

CYCOMM_SDK_API int32_t io_open(void);
CYCOMM_SDK_API int32_t io_read(void* data, size_t len);
CYCOMM_SDK_API int32_t io_write(const void* data, size_t len);
CYCOMM_SDK_API void io_close(void);

#ifdef __cplusplus
}
#endif
