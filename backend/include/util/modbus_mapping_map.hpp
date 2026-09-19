#pragma once
#include <limits.h>

#include <cstdint>
#include <cstring>
#include <iostream>

class ModbusMappingMap {
   public:
    unsigned short start_bits;             // 线圈寄存器的起始地址
    unsigned short nb_bits;                // 线圈寄存器的数量
    unsigned short start_input_bits;       // 离散输入寄存器的起始地址
    unsigned short nb_input_bits;          // 离散输入寄存器的数量
    unsigned short start_registers;        // 保持寄存器的起始地址
    unsigned short nb_registers;           // 保持寄存器的数量
    unsigned short start_input_registers;  // 输入寄存器的起始地址
    unsigned short nb_input_registers;     // 输入寄存器的数量

    uint8_t* tab_bits;              // 指向线圈寄存器的值
    uint8_t* tab_input_bits;        // 指向离散输入寄存器的值
    uint16_t* tab_registers;        // 指向输入寄存器的值
    uint16_t* tab_input_registers;  // 指向保持寄存器的值

   public:
    ModbusMappingMap()
        : nb_bits(0),
          nb_input_bits(0),
          nb_registers(0),
          nb_input_registers(0),
          start_bits(0),
          start_input_bits(0),
          start_registers(0),
          start_input_registers(0),
          tab_bits(nullptr),
          tab_input_bits(nullptr),
          tab_registers(nullptr),
          tab_input_registers(nullptr) {}
    ~ModbusMappingMap() {
        // modbus_mapping_free();
    }

    ModbusMappingMap(ushort start_bits, ushort nb_bits, ushort start_input_bits,
                     ushort nb_input_bits, ushort start_registers, ushort nb_registers,
                     ushort start_input_registers, ushort nb_input_registers) {
        // 线圈寄存器
        this->nb_bits = nb_bits;
        this->start_bits = start_bits;
        if ((this->nb_bits + this->start_bits) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_bits + start_bits > "
                "USHRT_MAX");
        }
        if (this->nb_bits > 0) {
            this->tab_bits = new uint8_t[this->nb_bits];
            memset(this->tab_bits, 0, this->nb_bits * sizeof(uint8_t));
        } else {
            this->tab_bits = nullptr;
        }

        // 离散输入寄存器
        this->nb_input_bits = nb_input_bits;
        this->start_input_bits = start_input_bits;
        if ((this->nb_input_bits + this->start_input_bits) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_input_bits + "
                "start_input_bits > USHRT_MAX");
        }
        if (this->nb_input_bits > 0) {
            this->tab_input_bits = new uint8_t[this->nb_input_bits];
            memset(this->tab_input_bits, 0, this->nb_input_bits * sizeof(uint8_t));
        } else {
            this->tab_input_bits = nullptr;
        }

        // 保持寄存器
        this->nb_registers = nb_registers;
        this->start_registers = start_registers;
        if ((this->nb_registers + this->start_registers) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_registers + "
                "start_registers > USHRT_MAX");
        }
        if (this->nb_registers > 0) {
            this->tab_registers = new uint16_t[this->nb_registers];
            memset(this->tab_registers, 0, this->nb_registers * sizeof(uint16_t));
        } else {
            this->tab_registers = nullptr;
        }

        // 输入寄存器
        this->nb_input_registers = nb_input_registers;
        this->start_input_registers = start_input_registers;
        if ((this->nb_input_registers + this->start_input_registers) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_input_registers + "
                "start_input_registers > USHRT_MAX");
        }
        if (this->nb_input_registers > 0) {
            this->tab_input_registers = new uint16_t[this->nb_input_registers];
            memset(this->tab_input_registers, 0, this->nb_input_registers * sizeof(uint16_t));
        } else {
            this->tab_input_registers = nullptr;
        }
    }
    ModbusMappingMap(ushort nb_bits, ushort nb_input_bits, ushort nb_registers,
                     ushort nb_input_registers) {
        // 线圈寄存器
        this->nb_bits = nb_bits;
        this->start_bits = 0;
        if ((this->nb_bits + this->start_bits) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_bits + start_bits > "
                "USHRT_MAX");
        }
        if (this->nb_bits > 0) {
            this->tab_bits = new uint8_t[this->nb_bits];
            memset(this->tab_bits, 0, this->nb_bits * sizeof(uint8_t));
        } else {
            this->tab_bits = nullptr;
        }

        // 离散输入寄存器
        this->nb_input_bits = nb_input_bits;
        this->start_input_bits = 0;
        if ((this->nb_input_bits + this->start_input_bits) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_input_bits + "
                "start_input_bits > USHRT_MAX");
        }
        if (this->nb_input_bits > 0) {
            this->tab_input_bits = new uint8_t[this->nb_input_bits];
            memset(this->tab_input_bits, 0, this->nb_input_bits * sizeof(uint8_t));
        } else {
            this->tab_input_bits = nullptr;
        }

        // 保持寄存器
        this->nb_registers = nb_registers;
        this->start_registers = 0;
        if ((this->nb_registers + this->start_registers) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_registers + "
                "start_registers > USHRT_MAX");
        }
        if (this->nb_registers > 0) {
            this->tab_registers = new uint16_t[this->nb_registers];
            memset(this->tab_registers, 0, this->nb_registers * sizeof(uint16_t));
        } else {
            this->tab_registers = nullptr;
        }

        // 输入寄存器
        this->nb_input_registers = nb_input_registers;
        this->start_input_registers = 0;
        if ((this->nb_input_registers + this->start_input_registers) > USHRT_MAX) {
            throw std::runtime_error(
                "ModbusMappingMap::modbus_mapping_new_start_address: nb_input_registers + "
                "start_input_registers > USHRT_MAX");
        }
        if (this->nb_input_registers > 0) {
            this->tab_input_registers = new uint16_t[this->nb_input_registers];
            memset(this->tab_input_registers, 0, this->nb_input_registers * sizeof(uint16_t));
        } else {
            this->tab_input_registers = nullptr;
        }
    }

    void modbus_mapping_free() {
        if (this->tab_bits != nullptr) {
            delete[] this->tab_bits;
        }

        if (this->tab_input_bits != nullptr) {
            delete[] this->tab_input_bits;
        }

        if (this->tab_input_registers != nullptr) {
            delete[] this->tab_input_registers;
        }

        if (this->tab_registers != nullptr) {
            delete[] this->tab_registers;
        }
    }
};