#pragma once
#include <mutex>

template<typename T>
class Singleton {
public:
    static T* inst() {
        std::call_once(_initFlag, create);
        return _instance;
    }

private:
    static void create() {
        _instance = new T();
    }

    static std::once_flag _initFlag;
    static T* _instance;
};

template<typename T>
std::once_flag Singleton<T>::_initFlag;

template<typename T>
T* Singleton<T>::_instance = nullptr;
