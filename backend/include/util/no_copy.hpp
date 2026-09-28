#pragma once

class NoCopy {
   public:
    /** @brief 构造不可复制基类。 */
    NoCopy() = default;

    /** @brief 销毁不可复制基类。 */
    ~NoCopy() = default;

    /**
     * @brief 禁止复制构造。
     * @param[in] other 待复制或移动的源对象。
     */
    NoCopy(const NoCopy& other) = delete;

    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待复制或移动的源对象。
     */
    NoCopy& operator=(const NoCopy& other) = delete;
};
