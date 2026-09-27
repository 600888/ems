#pragma once

// 判断浮点型数据是否为零
#define ISZERO(VAL) ((VAL < 0.000001) && (VAL > -0.000001))

/**
 * @brief 在 ISZERO 定义的容差内比较两个浮点数。
 * @param[in] a 左侧输入值。
 * @param[in] b 右侧输入值。
 * @return true 表示条件成立；false 表示不成立。
 */
constexpr bool isEqual(float a, float b) { return ISZERO(a - b); }
