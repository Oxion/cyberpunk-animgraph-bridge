#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs callback(context) under SEH — catches AV/DEP that C++ catch cannot. */
int Bridge_SehRun(void (*callback)(void*), void* context);

/* VirtualQuery: 1 if [address, address+byteCount) is committed readable. */
int Bridge_IsReadable(const void* address, size_t byteCount);

/* VirtualQuery + SEH memcpy of 16 bytes. Returns 1 on success. */
int Bridge_TryReadF4(const float* src, float* dst);

/* Read a pointer-sized value at addr (e.g. Handle.instance). */
int Bridge_TryReadPtr(const void* address, void** outPointer);

#ifdef __cplusplus
}
#endif
