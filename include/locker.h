/*
 * @Author: fs1n
 * @Email: fs1n@qq.com
 * @Description: 包装互斥锁、条件变量和信号量
 * @Date: 2023-03-21 11:50:49
 * @LastEditors: fs1n
 * @LastEditTime: 2023-03-21 15:49:32
 */
#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

class locker{
public:
    locker(){
        if(pthread_mutex_init(&m_mutex, nullptr) != 0){
            throw std::exception();
        }
    }

    ~locker(){
        if(pthread_mutex_destroy(&m_mutex) != 0){
            throw std::exception();
        }
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get(){ return &m_mutex; };
private:
    pthread_mutex_t m_mutex;
};

class condition{
public:
    condition(){
        if(pthread_cond_init(&m_cond, nullptr) != 0){
            throw std::exception();
        }
    }

    ~condition(){
        if(pthread_cond_destroy(&m_cond) != 0){
            throw std::exception();
        }
    }

    bool wait(pthread_mutex_t *mutex){
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }

    bool waitfor(pthread_mutex_t *mutex, struct timespec time){
        return pthread_cond_timedwait(&m_cond, mutex, &time) == 0;
    }

    bool singal(){
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

class sem{
public:
    sem(){ sem(0); }

    sem(int value){
        if(sem_init(&m_sem, 0, value) != 0){
            throw std::exception();
        }
    }

    ~sem(){
        if(sem_destroy(&m_sem) != 0){
            throw std::exception();
        }
    }

    // 获取信号量，不足则等待
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }

    // 增加信号量
    bool post(){
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

#endif // LOCKER_H