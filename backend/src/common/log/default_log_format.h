#pragma once

// 整数类型文件行号
#ifdef _WIN32
#define __FILENAME__ (strrchr(__FILE__, '\\') ? (strrchr(__FILE__, '\\') + 1) : __FILE__)
#else
#define __FILENAME__ (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)
#endif

// 格式化
#ifndef FORMATTED
#define FORMATTED(format)                  \
    (std::string("[")                      \
         .append(__FILENAME__)             \
         .append(":")                      \
         .append(std::to_string(__LINE__)) \
         .append("]<")                     \
         .append(__FUNCTION__)             \
         .append("> - ")                   \
         .append(format)                   \
         .c_str())
#endif