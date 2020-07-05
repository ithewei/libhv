#ifndef HV_SINGLETON_H_
#define HV_SINGLETON_H_

#define DISABLE_COPY(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete;

#define SINGLETON_DECL(Class) \
    public: \
        static Class* instance(); \
        static void exitInstance(); \
    private: \
        DISABLE_COPY(Class) \
        static Class* s_pInstance;

#define SINGLETON_IMPL(Class) \
    Class* Class::s_pInstance = NULL; \
    Class* Class::instance() { \
        if (s_pInstance == NULL) { \
            s_pInstance = new Class; \
        } \
        return s_pInstance; \
    } \
    void Class::exitInstance() { \
        if (s_pInstance) {  \
            delete s_pInstance; \
            s_pInstance = NULL; \
        }   \
    }

#endif // HV_SINGLETON_H_
