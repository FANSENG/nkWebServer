/*
 * @Author: fs1n
 * @Email: fs1n@qq.com
 * @Description: {To be filled in}
 * @Date: 2023-03-21 11:50:58
 * @LastEditors: fs1n
 * @LastEditTime: 2023-03-21 15:51:08
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "locker.h"
#include <pthread.h>
#include <list>
#include <cstdio>
#include <exception>

template<typename T>
class threadpool{
public:
    threadpool();
    /**
     * @brief 
     * @param {int} thread_number
     * @param {int} max_requests
     * @return {*}
     */    
    threadpool(int thread_number, int max_requests);
    ~threadpool();
    bool append(T* request);
private:
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number;        // 线程池数量
    pthread_t *m_threads;       // 线程池列表
    int m_max_requests;         // 最大请求数
    std::list<T*> m_workqueue;  // 请求列表
    locker m_queuelocker;       // 请求列表锁
    sem m_queuesem;             // 请求信号量，判断是否有请求待处理 
    bool m_run;                // 是否需要结束任务
};

template<typename T>
threadpool<T>::threadpool(){
    threadpool<T>::threadpool(8, 10000);
}

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests)
: m_thread_number(thread_number), m_max_requests(max_requests), m_run(true), m_threads(nullptr){
    if(m_thread_number <= 0 || m_max_requests <=0){
        throw std::exception();
    } 

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    for(int i = 0; i < m_thread_number; i++){
        if(pthread_create(m_threads + i, nullptr, worker, nullptr) != 0){
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;    // 必须使用 [] 删除，否则会内存泄漏
    m_run = false;
}

template<typename T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();
    if(m_queuesem.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuesem.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(m_run){
        m_queuesem.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) continue;
        request->process();
    }
}

#endif