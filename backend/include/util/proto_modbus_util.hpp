#pragma once
#include <assert.h>             // assert
#include <bits/stdint-uintn.h>  // uint16_t
#include <stdint.h>
#include <sys/time.h>  // timeval
#include <unistd.h>    // ssize_t

#include <cstring>

#define MODBUS_GET_HIGH_BYTE(data) (((data) >> 8) & 0xFF)
#define MODBUS_GET_LOW_BYTE(data) ((data) & 0xFF)
#define MODBUS_GET_INT64_FROM_INT16(tab_int16, index)                                \
    (((int64_t)tab_int16[(index)] << 48) | ((int64_t)tab_int16[(index) + 1] << 32) | \
     ((int64_t)tab_int16[(index) + 2] << 16) | (int64_t)tab_int16[(index) + 3])
#define MODBUS_GET_INT32_FROM_INT16(tab_int16, index) \
    (((int32_t)tab_int16[(index)] << 16) | (int32_t)tab_int16[(index) + 1])
#define MODBUS_GET_INT16_FROM_INT8(tab_int8, index) \
    (((int16_t)tab_int8[(index)] << 8) | (int16_t)tab_int8[(index) + 1])
#define MODBUS_SET_INT16_TO_INT8(tab_int8, index, value)         \
    do {                                                         \
        ((int8_t*)(tab_int8))[(index)] = (int8_t)((value) >> 8); \
        ((int8_t*)(tab_int8))[(index) + 1] = (int8_t)(value);    \
    } while (0)
#define MODBUS_SET_INT32_TO_INT16(tab_int16, index, value)           \
    do {                                                             \
        ((int16_t*)(tab_int16))[(index)] = (int16_t)((value) >> 16); \
        ((int16_t*)(tab_int16))[(index) + 1] = (int16_t)(value);     \
    } while (0)
#define MODBUS_SET_INT64_TO_INT16(tab_int16, index, value)               \
    do {                                                                 \
        ((int16_t*)(tab_int16))[(index)] = (int16_t)((value) >> 48);     \
        ((int16_t*)(tab_int16))[(index) + 1] = (int16_t)((value) >> 32); \
        ((int16_t*)(tab_int16))[(index) + 2] = (int16_t)((value) >> 16); \
        ((int16_t*)(tab_int16))[(index) + 3] = (int16_t)(value);         \
    } while (0)

/** @brief 在 Modbus 寄存器与字节序列之间转换位和浮点数。 */
class ModbusUtil {
   public:
    /**
     * @brief 将一个字节的 8 位展开到目标位数组。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] idx 元素索引。
     * @param[in] value 输入值。
     */
    static void modbus_set_bits_from_byte(uint8_t* dest, int idx, const uint8_t value) {
        int i;

        for (i = 0; i < 8; i++) {
            dest[idx + i] = (value & (1 << i)) ? 1 : 0;
        }
    }

    /**
     * @brief 将字节数组中的指定数量位展开到目标位数组。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] idx 元素索引。
     * @param[in] nb_bits 线圈数量。
     * @param[in] tab_byte 制表符字节。
     */
    static void modbus_set_bits_from_bytes(uint8_t* dest, int idx, unsigned int nb_bits,
                                           const uint8_t* tab_byte) {
        unsigned int i;
        int shift = 0;

        for (i = idx; i < idx + nb_bits; i++) {
            dest[i] = tab_byte[(i - idx) / 8] & (1 << shift) ? 1 : 0;
            // 0 ~ 7 偏移循环
            shift++;
            shift %= 8;
        }
    }

    /**
     * @brief 从最多 8 个位数组元素组合出一个字节。
     * @param[in] src 源数据缓冲区。
     * @param[in] idx 元素索引。
     * @param[in] nb_bits 线圈数量。
     * @return 得到的8 位无符号整数。
     */
    static uint8_t modbus_get_byte_from_bits(const uint8_t* src, int idx, unsigned int nb_bits) {
        unsigned int i;
        uint8_t value = 0;

        // 位个数最大为8
        if (nb_bits > 8) {
            // Assert is ignored if NDEBUG is set.
            assert(nb_bits < 8);
            nb_bits = 8;
        }

        for (i = 0; i < nb_bits; i++) {
            value |= (src[idx + i] << i);
        }

        return value;
    }

    /**
     * @brief 按 ABCD 字节序从两个寄存器读取浮点数。
     * @param[in] src 源数据缓冲区。
     * @return 按指定类型解释的数值。
     */
    static float modbus_get_float_abcd(const uint16_t* src) {
        float f;
        uint32_t i;
        uint8_t a, b, c, d;

        // Mind: src contains 16-bit numbers in processor-endianness, hence
        //       we use shift operations and do not access memory directly
        a = (src[0] >> 8) & 0xFF;  // high byte of first word
        b = (src[0] >> 0) & 0xFF;  // low byte of first word
        c = (src[1] >> 8) & 0xFF;  // high byte of second word
        d = (src[1] >> 0) & 0xFF;  // low byte of second word

        // we assemble 32bit integer always in abcd order via shift operations
        i = (a << 24) | (b << 16) | (c << 8) | (d << 0);
        memcpy(&f, &i, 4);

        return f;
    }

    /**
     * @brief 按 DCBA 字节序从两个寄存器读取浮点数。
     * @param[in] src 源数据缓冲区。
     * @return 按指定类型解释的数值。
     */
    static float modbus_get_float_dcba(const uint16_t* src) {
        float f;
        uint32_t i;
        uint8_t a, b, c, d;

        // byte order is defined when reading from src: dcba
        d = (src[0] >> 8) & 0xFF;
        c = (src[0] >> 0) & 0xFF;
        b = (src[1] >> 8) & 0xFF;
        a = (src[1] >> 0) & 0xFF;

        // we assemble 32bit integer always in abcd order via shift operations
        i = (a << 24) | (b << 16) | (c << 8) | (d << 0);
        memcpy(&f, &i, 4);

        return f;
    }

    /**
     * @brief 按 BADC 字节序从两个寄存器读取浮点数。
     * @param[in] src 源数据缓冲区。
     * @return 按指定类型解释的数值。
     */
    static float modbus_get_float_badc(const uint16_t* src) {
        float f;
        uint32_t i;
        uint8_t a, b, c, d;

        // byte order is defined when reading from src: badc
        b = (src[0] >> 8) & 0xFF;
        a = (src[0] >> 0) & 0xFF;
        d = (src[1] >> 8) & 0xFF;
        c = (src[1] >> 0) & 0xFF;

        // we assemble 32bit integer always in abcd order via shift operations
        i = (a << 24) | (b << 16) | (c << 8) | (d << 0);
        memcpy(&f, &i, 4);

        return f;
    }

    /**
     * @brief 按 CDAB 字节序从两个寄存器读取浮点数。
     * @param[in] src 源数据缓冲区。
     * @return 按指定类型解释的数值。
     */
    static float modbus_get_float_cdab(const uint16_t* src) {
        float f;
        uint32_t i;
        uint8_t a, b, c, d;

        // byte order is defined when reading from src: cdab
        c = (src[0] >> 8) & 0xFF;
        d = (src[0] >> 0) & 0xFF;
        a = (src[1] >> 8) & 0xFF;
        b = (src[1] >> 0) & 0xFF;

        // we assemble 32bit integer always in abcd order via shift operations
        i = (a << 24) | (b << 16) | (c << 8) | (d << 0);
        memcpy(&f, &i, 4);

        return f;
    }

    /**
     * @brief 按兼容的 CDAB 字节序读取浮点数。
     * @param[in] src 源数据缓冲区。
     * @return 按指定类型解释的数值。
     */
    static float modbus_get_float(const uint16_t* src) { return modbus_get_float_cdab(src); }

    /**
     * @brief 按 ABCD 字节序写入两个寄存器。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_abcd(float f, uint16_t* dest) {
        // The straight-forward type conversion won't work because of type-punned pointer aliasing warning
        // uint32_t i = *(uint32_t*)(&f);
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (a << 8) | b;
        dest[1] = (c << 8) | d;
    }

    /**
     * @brief 按 ABCD 字节序写入四个字节。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_abcd_8(float f, uint8_t* dest) {
        // The straight-forward type conversion won't work because of type-punned pointer aliasing warning
        // uint32_t i = *(uint32_t*)(&f);
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = a;
        dest[1] = b;
        dest[2] = c;
        dest[3] = d;

        // memcpy 或 联合体
        // memcpy(dest, &f, 4);
    }

    /**
     * @brief 按 DCBA 字节序写入两个寄存器。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_dcba(float f, uint16_t* dest) {
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (d << 8) | c;
        dest[1] = (b << 8) | a;
    }

    /**
     * @brief 按 DCBA 字节序写入四个字节。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_dcba_8(float f, uint8_t* dest) {
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = d;
        dest[1] = c;
        dest[2] = b;
        dest[3] = a;
    }

    /**
     * @brief 按 BADC 字节序写入两个寄存器。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_badc(float f, uint16_t* dest) {
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (b << 8) | a;
        dest[1] = (d << 8) | c;
    }

    /**
     * @brief 按 BADC 字节序写入四个字节。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_badc_8(float f, uint8_t* dest) {
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = b;
        dest[1] = a;
        dest[2] = d;
        dest[3] = c;
    }

    /**
     * @brief 按 CDAB 字节序写入两个寄存器。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_cdab(float f, uint16_t* dest) {
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (c << 8) | d;
        dest[1] = (a << 8) | b;
    }

    /**
     * @brief 按 CDAB 字节序写入四个字节。
     * @param[in] f 待编码的浮点数。
     * @param[out] dest 目标数据缓冲区。
     */
    static void modbus_set_float_cdab_8(float f, uint8_t* dest) {
        float* fptr = &f;
        uint32_t* iptr = (uint32_t*)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = c;
        dest[1] = d;
        dest[2] = a;
        dest[3] = b;
    }

    /**
     * @brief 将字符串复制到固定容量的缓冲区并以 NUL 结尾。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] src 源字符串。
     * @param[in] dest_size 目标缓冲区容量，包含结尾的 NUL。
     * @return 源字符串的长度；返回值不包括结尾的 NUL。
     */
    static size_t strlcpy(char* dest, const char* src, size_t dest_size) {
        char* d = dest;
        const char* s = src;
        size_t n = dest_size;

        /* Copy as many bytes as will fit */
        if (n != 0 && --n != 0) {
            do {
                if ((*d++ = *s++) == 0) break;
            } while (--n != 0);
        }

        /* Not enough room in dest, add NUL and traverse rest of src */
        if (n == 0) {
            if (dest_size != 0) *d = '\0'; /* NUL-terminate dest */
            while (*s++);
        }

        return (s - src - 1); /* count does not include NUL */
    }
#endif
};
