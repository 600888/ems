#pragma once
#include "def/base_def.h"
constexpr int INITIAL_CAPACITY = 16;

#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

const int MAX_ITEMS = 100; // 最大存储项数

template <typename Key, typename Value>
struct SharedMemoryNode {
    Key key;
    Value value;
    bool is_occupied;
};

template <typename Key, typename Value>
class SharedMemoryMap {
private:
    SharedMemoryNode<Key, Value>* nodes;
    int shm_fd;
    const char* shm_name;

public:
    SharedMemoryMap(const char* name)
        : shm_name(name)
    {
        shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
        ftruncate(shm_fd, sizeof(SharedMemoryNode<Key, Value>) * MAX_ITEMS);
        nodes = static_cast<SharedMemoryNode<Key, Value>*>(
            mmap(0, sizeof(SharedMemoryNode<Key, Value>) * MAX_ITEMS, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
        if (nodes == MAP_FAILED) {
            std::cerr << "mmap failed" << std::endl;
            exit(1);
        }
        // 初始化节点
        for (int i = 0; i < MAX_ITEMS; ++i) {
            nodes[i].is_occupied = false;
        }
    }

    ~SharedMemoryMap()
    {
        munmap(nodes, sizeof(SharedMemoryNode<Key, Value>) * MAX_ITEMS);
        close(shm_fd);
        shm_unlink(shm_name);
    }

    // 插入键值对
    void insert(const Key& key, const Value& value)
    {
        for (int i = 0; i < MAX_ITEMS; ++i) {
            if (!nodes[i].is_occupied) {
                nodes[i].key = key;
                nodes[i].value = value;
                nodes[i].is_occupied = true;
                return;
            }
        }
        std::cerr << "Shared memory map is full" << std::endl;
    }

    // 查找值
    bool find(const Key& key, Value& value) const
    {
        for (int i = 0; i < MAX_ITEMS; ++i) {
            if (nodes[i].is_occupied && nodes[i].key == key) {
                value = nodes[i].value;
                return true;
            }
        }
        return false;
    }

    // 删除键值对
    bool erase(const Key& key)
    {
        for (int i = 0; i < MAX_ITEMS; ++i) {
            if (nodes[i].is_occupied && nodes[i].key == key) {
                nodes[i].is_occupied = false;
                return true;
            }
        }
        return false;
    }
};