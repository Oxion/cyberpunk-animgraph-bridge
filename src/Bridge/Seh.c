#include "Bridge/Seh.h"

#include <string.h>
#include <windows.h>

int Bridge_SehRun(void (*callback)(void*), void* context)
{
    __try
    {
        callback(context);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return 1;
}

int Bridge_IsReadable(const void* address, size_t byteCount)
{
    const char* cursor = (const char*)address;
    size_t bytesRemaining = byteCount;

    if (!address || byteCount == 0)
        return 0;

    while (bytesRemaining > 0)
    {
        MEMORY_BASIC_INFORMATION memoryInfo;
        if (!VirtualQuery(cursor, &memoryInfo, sizeof(memoryInfo)))
            return 0;
        if (memoryInfo.State != MEM_COMMIT)
            return 0;
        if (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            return 0;

        const DWORD protection = memoryInfo.Protect & 0xFF;
        const int readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                             protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                             protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (!readable)
            return 0;

        const char* regionEnd = (const char*)memoryInfo.BaseAddress + memoryInfo.RegionSize;
        size_t chunkSize = (size_t)(regionEnd - cursor);
        if (chunkSize > bytesRemaining)
            chunkSize = bytesRemaining;
        cursor += chunkSize;
        bytesRemaining -= chunkSize;
    }
    return 1;
}

int Bridge_TryReadF4(const float* src, float* dst)
{
    if (!src || !dst)
        return 0;
    if (!Bridge_IsReadable(src, sizeof(float) * 4))
        return 0;

    __try
    {
        memcpy(dst, src, sizeof(float) * 4);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

int Bridge_TryReadPtr(const void* address, void** outPointer)
{
    if (!address || !outPointer)
        return 0;
    if (!Bridge_IsReadable(address, sizeof(void*)))
        return 0;

    __try
    {
        memcpy(outPointer, address, sizeof(void*));
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}
