#pragma once

#define MakeWord(l, h) ((unsigned short)((unsigned char)(l) + (unsigned char)(h)*256))
#define MakeLong(l, h) ((unsigned int)((unsigned short)(l) + (unsigned short)(h)*65536))
#define LoByte(w) ((unsigned char)((unsigned short)(w) % 256))
#define HiByte(w) ((unsigned char)((unsigned short)(w) / 256))
#define LoWord(l) ((unsigned short)((unsigned int)(l) % 65536))
#define HiWord(l) ((unsigned short)((unsigned int)(l) / 65536))

#define BIT_ENABLED(VAL, BIT) (((VAL) & (BIT)) != 0) // 判断某位是否有效
#define SET_BITS(VAL, BITS) (VAL |= (BITS))          // 设置某位
#define SWAP_BYTE_2(X) ((((X) & 0x00FF) << 8) | (((X) & 0xFF00) >> 8))