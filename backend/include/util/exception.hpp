#pragma once
#include <stdexcept>
#include <string>

#ifndef EXCEPTION_FMT

#ifdef _WIN32
#define __FILENAME__ (strrchr(__FILE__, '\\') ? (strrchr(__FILE__, '\\') + 1) : __FILE__)
#else
#define __FILENAME__ (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)
#endif

#define EXCEPTION_FMT(fmt)                 \
    (std::string("[")                      \
         .append(__FILENAME__)             \
         .append(":")                      \
         .append(std::to_string(__LINE__)) \
         .append("]<")                     \
         .append(__FUNCTION__)             \
         .append("> - ")                   \
         .append(fmt)                      \
         .c_str())

#endif

#define THROW_RUNTIME_ERROR(fmt) throw std::runtime_error(EXCEPTION_FMT(fmt))