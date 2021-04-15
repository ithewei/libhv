#ifndef HV_SINGLETON_H_
#define HV_SINGLETON_H_

#include <mutex>

#define DISABLE_COPY(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete;

#define SINGLETON_DECL(Class) \
    public: \
        static Class* instance(); \
        static void exitInstance(); \
    private: \
        DISABLE_COPY(Class) \
        static Class* s_pInstance; \
        static std::mutex s_mutex;

#define SINGLETON_IMPL(Class) \
    Class* Class::s_pInstance = NULL; \
    std::mutex Class::s_mutex; \
    Class* Class::instance() { \
        if (s_pInstance == NULL) { \
            s_mutex.lock(); \
            if (s_pInstance == NULL) { \
                s_pInstance = new Class; \
            } \
            s_mutex.unlock(); \
        } \
        return s_pInstance; \
    } \
    void Class::exitInstance() { \
        s_mutex.lock(); \
        if (s_pInstance) {  \
            delete s_pInstance; \
            s_pInstance = NULL; \
        }   \
        s_mutex.unlock(); \
    }

#endif // HV_SINGLETON_H_
