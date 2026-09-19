#pragma once

// 判断浮点型数据是否为零
#define ISZERO(VAL) ((VAL < 0.000001) && (VAL > -0.000001))

constexpr bool isEqual(float a, float b) { return ISZERO(a - b); }