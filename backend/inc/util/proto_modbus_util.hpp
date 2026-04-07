#pragma once
#include <bits/stdint-uintn.h> // uint16_t
#include <sys/time.h>          // timeval
#include <stdint.h>
#include <unistd.h> // ssize_t
#include <assert.h> // assert
#include <cstring>

#define MODBUS_GET_HIGH_BYTE(data) (((data) >> 8) & 0xFF)
#define MODBUS_GET_LOW_BYTE(data) ((data) & 0xFF)
#define MODBUS_GET_INT64_FROM_INT16(tab_int16, index) \
    (((int64_t)tab_int16[(index)] << 48) | ((int64_t)tab_int16[(index) + 1] << 32) | ((int64_t)tab_int16[(index) + 2] << 16) | (int64_t)tab_int16[(index) + 3])
#define MODBUS_GET_INT32_FROM_INT16(tab_int16, index) \
    (((int32_t)tab_int16[(index)] << 16) | (int32_t)tab_int16[(index) + 1])
#define MODBUS_GET_INT16_FROM_INT8(tab_int8, index) \
    (((int16_t)tab_int8[(index)] << 8) | (int16_t)tab_int8[(index) + 1])
#define MODBUS_SET_INT16_TO_INT8(tab_int8, index, value)          \
    do {                                                          \
        ((int8_t *)(tab_int8))[(index)] = (int8_t)((value) >> 8); \
        ((int8_t *)(tab_int8))[(index) + 1] = (int8_t)(value);    \
    } while (0)
#define MODBUS_SET_INT32_TO_INT16(tab_int16, index, value)            \
    do {                                                              \
        ((int16_t *)(tab_int16))[(index)] = (int16_t)((value) >> 16); \
        ((int16_t *)(tab_int16))[(index) + 1] = (int16_t)(value);     \
    } while (0)
#define MODBUS_SET_INT64_TO_INT16(tab_int16, index, value)                \
    do {                                                                  \
        ((int16_t *)(tab_int16))[(index)] = (int16_t)((value) >> 48);     \
        ((int16_t *)(tab_int16))[(index) + 1] = (int16_t)((value) >> 32); \
        ((int16_t *)(tab_int16))[(index) + 2] = (int16_t)((value) >> 16); \
        ((int16_t *)(tab_int16))[(index) + 3] = (int16_t)(value);         \
    } while (0)

class ModbusUtil {
public:
    // 从一个字节获取 8 个位数据填充到 dest 的 idx 位置后面
    static void modbus_set_bits_from_byte(uint8_t *dest, int idx, const uint8_t value)
    {
        int i;

        for (i = 0; i < 8; i++) {
            dest[idx + i] = (value & (1 << i)) ? 1 : 0;
        }
    }

    // 从 tab_byte 字节组获取 nb_bits 个位数据填充到 dest 的 idx 位置后面
    static void modbus_set_bits_from_bytes(uint8_t *dest,
                                           int idx,
                                           unsigned int nb_bits,
                                           const uint8_t *tab_byte)
    {
        unsigned int i;
        int shift = 0;

        for (i = idx; i < idx + nb_bits; i++) {
            dest[i] = tab_byte[(i - idx) / 8] & (1 << shift) ? 1 : 0;
            // 0 ~ 7 偏移循环
            shift++;
            shift %= 8;
        }
    }

    // 从 src 的 idx 位置处取 nb_bits 个位数据获取一个字节值
    static uint8_t modbus_get_byte_from_bits(const uint8_t *src,
                                             int idx,
                                             unsigned int nb_bits)
    {
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

    // 从4字节返回一个float 值，无需任何转换 (ABCD)
    static float modbus_get_float_abcd(const uint16_t *src)
    {
        float f;
        uint32_t i;
        uint8_t a, b, c, d;

        // Mind: src contains 16-bit numbers in processor-endianness, hence
        //       we use shift operations and do not access memory directly
        a = (src[0] >> 8) & 0xFF; // high byte of first word
        b = (src[0] >> 0) & 0xFF; // low byte of first word
        c = (src[1] >> 8) & 0xFF; // high byte of second word
        d = (src[1] >> 0) & 0xFF; // low byte of second word

        // we assemble 32bit integer always in abcd order via shift operations
        i = (a << 24) | (b << 16) | (c << 8) | (d << 0);
        memcpy(&f, &i, 4);

        return f;
    }

    // 从4字节返回一个float 值，按 DCBA 字序
    static float modbus_get_float_dcba(const uint16_t *src)
    {
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

    // Get a float from 4 bytes (Modbus) with swapped bytes (BADC)
    static float modbus_get_float_badc(const uint16_t *src)
    {
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

    // Get a float from 4 bytes (Modbus) with swapped bytes (CDAB)
    static float modbus_get_float_cdab(const uint16_t *src)
    {
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

    // DEPRECATED - Get a float from 4 bytes in sort of Modbus format
    static float modbus_get_float(const uint16_t *src)
    {
        return modbus_get_float_cdab(src);
    }

    /* Set a float to 4 bytes for Modbus w/o any conversion (ABCD) */
    static void modbus_set_float_abcd(float f, uint16_t *dest)
    {
        // The straight-forward type conversion won't work because of type-punned pointer aliasing warning
        // uint32_t i = *(uint32_t*)(&f);
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (a << 8) | b;
        dest[1] = (c << 8) | d;
    }

    static void modbus_set_float_abcd_8(float f, uint8_t *dest)
    {
        // The straight-forward type conversion won't work because of type-punned pointer aliasing warning
        // uint32_t i = *(uint32_t*)(&f);
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
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

    /* Set a float to 4 bytes for Modbus with byte and word swap conversion (DCBA) */
    static void modbus_set_float_dcba(float f, uint16_t *dest)
    {
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (d << 8) | c;
        dest[1] = (b << 8) | a;
    }
    static void modbus_set_float_dcba_8(float f, uint8_t *dest)
    {
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
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

    /* Set a float to 4 bytes for Modbus with byte swap conversion (BADC) */
    static void modbus_set_float_badc(float f, uint16_t *dest)
    {
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (b << 8) | a;
        dest[1] = (d << 8) | c;
    }

    static void modbus_set_float_badc_8(float f, uint8_t *dest)
    {
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
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

    /* Set a float to 4 bytes for Modbus with word swap conversion (CDAB) */
    static void modbus_set_float_cdab(float f, uint16_t *dest)
    {
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
        uint32_t i = *iptr;
        uint8_t a, b, c, d;

        a = (i >> 24) & 0xFF;
        b = (i >> 16) & 0xFF;
        c = (i >> 8) & 0xFF;
        d = (i >> 0) & 0xFF;

        dest[0] = (c << 8) | d;
        dest[1] = (a << 8) | b;
    }

    static void modbus_set_float_cdab_8(float f, uint8_t *dest)
    {
        float *fptr = &f;
        uint32_t *iptr = (uint32_t *)fptr;
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

    /* DEPRECATED - Set a float to 4 bytes in a sort of Modbus format! */
    static void modbus_set_float(float f, uint16_t *dest)
    {
        modbus_set_float_cdab(f, dest);
    }

#ifndef HAVE_STRLCPY
    /*
     * Function strlcpy was originally developed by
     * Todd C. Miller <Todd.Miller@courtesan.com> to simplify writing secure code.
     * See ftp://ftp.openbsd.org/pub/OpenBSD/src/lib/libc/string/strlcpy.3
     * for more information.
     *
     * Thank you Ulrich Drepper... not!
     *
     * Copy src to string dest of size dest_size.  At most dest_size-1 characters
     * will be copied.  Always NUL terminates (unless dest_size == 0).  Returns
     * strlen(src); if retval >= dest_size, truncation occurred.
     */
    static size_t strlcpy(char *dest, const char *src, size_t dest_size)
    {
        char *d = dest;
        const char *s = src;
        size_t n = dest_size;

        /* Copy as many bytes as will fit */
        if (n != 0 && --n != 0) {
            do {
                if ((*d++ = *s++) == 0)
                    break;
            } while (--n != 0);
        }

        /* Not enough room in dest, add NUL and traverse rest of src */
        if (n == 0) {
            if (dest_size != 0)
                *d = '\0'; /* NUL-terminate dest */
            while (*s++);
        }

        return (s - src - 1); /* count does not include NUL */
    }
#endif
};