#ifndef _SINGLE_BASE_H_
#define _SINGLE_BASE_H_

#include <QMutex>

//*******************************
//single mode base class
//*******************************


template <class T>
class Singleton
{

private:
    static T* m_pInstance;
    //forbidden copy/=
    Singleton(const Singleton& src){}
    Singleton &operator=(const Singleton& src){}
    //free
    class Garbo
    {
    public:
        ~Garbo()
        {
            if (Singleton::m_pInstance)
            {
                delete Singleton::m_pInstance;
                Singleton::m_pInstance = NULL;
            }
        }
    };
    static Garbo garbo;
    static QMutex  mutex_create;
protected:
    Singleton(){}
    ~Singleton(){}

public:

    static T* getInstance()
    {
        if (m_pInstance == NULL)
        {
            QMutexLocker locker(&mutex_create);
            if (m_pInstance == NULL)
            {
                m_pInstance = new T();
            }
        }
        return m_pInstance;
    }
};

template <class T>
T* Singleton<T>::m_pInstance = NULL;
template <class T>
QMutex Singleton<T>::mutex_create;

#endif // _SINGLE_BASE_H_
