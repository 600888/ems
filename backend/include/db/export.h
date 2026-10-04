#pragma once

#if defined(_WIN32)
#if defined(EMS_DB_BUILD)
#define EMS_DB_API __declspec(dllexport)
#else
#define EMS_DB_API __declspec(dllimport)
#endif
#else
#define EMS_DB_API
#endif
