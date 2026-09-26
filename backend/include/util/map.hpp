#pragma once
#include "def/base_def.h"
constexpr int INITIAL_CAPACITY = 16;

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

const int MAX_ITEMS = 100;  // 最大存储项数

template <typename Key, typename Value>
struct SharedMemoryNode {
    Key key;
    Value value;
    bool is_occupied;
};

template <typename Key, typename Value>
/** @brief 在进程间共享固定容量键值节点。 */
class SharedMemoryMap {
   private:
    SharedMemoryNode<Key, Value>* nodes;
    int shm_fd;
    const char* shm_name;

   public:
    /**
     * @brief 创建或打开命名共享内存并初始化节点。
     * @param[in] name 对象名称。
     */
    SharedMemoryMap(const char* name) : shm_name(name) {
        shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
        ftruncate(shm_fd, sizeof(SharedMemoryNode<Key, Value>) * MAX_ITEMS);
        nodes = static_cast<SharedMemoryNode<Key, Value>*>(
            mmap(0, sizeof(SharedMemoryNode<Key, Value>) * MAX_ITEMS, PROT_READ | PROT_WRITE,
                 MAP_SHARED, shm_fd, 0));
        if (nodes == MAP_FAILED) {
            std::cerr << "mmap failed" << std::endl;
            exit(1);
        }
        // 初始化节点
        for (int i = 0; i < MAX_ITEMS; ++i) {
            nodes[i].is_occupied = false;
        }
    }

    /** @brief 解除映射、关闭句柄并删除命名共享内存。 */
    ~SharedMemoryMap() {
        munmap(nodes, sizeof(SharedMemoryNode<Key, Value>) * MAX_ITEMS);
        close(shm_fd);
        shm_unlink(shm_name);
    }

    /**
     * @brief 插入键值对；容量已满时输出错误。
     * @param[in] key Redis 键。
     * @param[in] value 输入值。
     */
    void insert(const Key& key, const Value& value) {
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

    /**
     * @brief 按键查找并输出值；不存在返回 false。
     * @param[in] key Redis 键。
     * @param[out] value 接收读取结果的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool find(const Key& key, Value& value) const {
        for (int i = 0; i < MAX_ITEMS; ++i) {
            if (nodes[i].is_occupied && nodes[i].key == key) {
                value = nodes[i].value;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 按键删除节点；不存在返回 false。
     * @param[in] key Redis 键。
     * @return true 表示操作成功；false 表示失败。
     */
    bool erase(const Key& key) {
        for (int i = 0; i < MAX_ITEMS; ++i) {
            if (nodes[i].is_occupied && nodes[i].key == key) {
                nodes[i].is_occupied = false;
                return true;
            }
        }
        return false;
    }
};
