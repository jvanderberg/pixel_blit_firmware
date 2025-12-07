#include "ff.h"
#include "diskio.h"

// Stub implementation of get_fattime for FatFS
// Returns a fixed timestamp (Jan 1, 2025) to avoid hardware_rtc dependency
DWORD get_fattime (void)
{
    return ((DWORD)(2025 - 1980) << 25) | // Year 2025
           ((DWORD)1 << 21) |             // Month 1
           ((DWORD)1 << 16) |             // Day 1
           ((DWORD)12 << 11) |            // Hour 12
           ((DWORD)0 << 5) |              // Min 0
           ((DWORD)0 >> 1);               // Sec 0
}
