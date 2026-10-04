#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::iec104 {

/** @brief IEC104 ASDU 类型标识，数值与 IEC60870 编码一致。 */
enum class AsduType : int {
    M_SP_NA_1 = 1,
    M_SP_TA_1 = 2,
    M_DP_NA_1 = 3,
    M_DP_TA_1 = 4,
    M_ME_NA_1 = 9,
    M_ME_TA_1 = 10,
    M_ME_NB_1 = 11,
    M_ME_TB_1 = 12,
    M_ME_NC_1 = 13,
    M_ME_TC_1 = 14,
    M_ME_ND_1 = 21,
    M_SP_TB_1 = 30,
    M_DP_TB_1 = 31,
    M_ME_TD_1 = 34,
    M_ME_TE_1 = 35,
    M_ME_TF_1 = 36,
    C_SC_NA_1 = 45,
    C_DC_NA_1 = 46,
    C_RC_NA_1 = 47,
    C_SE_NA_1 = 48,
    C_SE_NB_1 = 49,
    C_SE_NC_1 = 50,
    C_SC_TA_1 = 58,
    C_DC_TA_1 = 59,
    C_RC_TA_1 = 60,
    C_SE_TA_1 = 61,
    C_SE_TB_1 = 62,
    C_SE_TC_1 = 63,
};

/** @brief 监视与控制 ASDU 的数值类别。 */
enum class ValueFamily : int {
    SinglePoint = 1,
    DoublePoint = 2,
    Step = 3,
    Normalized = 4,
    Scaled = 5,
    ShortFloat = 6,
};

/** @brief IEC104 品质描述位掩码。 */
enum class QualityMask : int {
    Overflow = 1,
    Supported = 0xf1,
};

/** @brief IEC104 双点遥信状态及双命令状态。 */
enum class DoublePointState : int {
    Indeterminate = 0,
    Off = 1,
    On = 2,
    Invalid = 3,
};

}  // namespace ems::net::iec104
