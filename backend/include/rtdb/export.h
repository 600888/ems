#pragma once

#if defined(_WIN32)
#if defined(EMS_RTDB_BUILD)
#define EMS_RTDB_API __declspec(dllexport)
#else
#define EMS_RTDB_API __declspec(dllimport)
#endif
#else
#define EMS_RTDB_API __attribute__((visibility("default")))
#endif
