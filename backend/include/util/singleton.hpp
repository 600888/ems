#pragma once
#include <mutex>

template <typename T>
class Singleton {
   public:
    /**
     * @brief 返回在首次调用时创建的单例指针。
     * @return 对应对象的指针。
     */
    static T* inst() {
        std::call_once(_initFlag, create);
        return _instance;
    }

   private:
    /** @brief 分配单例对象，由 call_once 保证只调用一次。 */
    static void create() { _instance = new T(); }

    static std::once_flag _initFlag;
    static T* _instance;
};

template <typename T>
std::once_flag Singleton<T>::_initFlag;

template <typename T>
T* Singleton<T>::_instance = nullptr;
