#ifndef C_THREAD_H
#define C_THREAD_H

/*
 * Thread base class with priority and stop signaling support.
 */

#include <pthread.h>
#include <iostream>
#include <atomic>

using namespace std;

class C_Thread {

    pthread_t m_thread;           
    pthread_attr_t m_attributes;  
    int m_priority;               
    std::atomic<bool> m_stopRequested{false};

    static void* internalRun(void* arg);

public:
    C_Thread(int priority = 0);
    virtual ~C_Thread();
    bool start();
    void join();
    void detach();
    void cancel();
    void requestStop();
    bool stopRequested() const;
    virtual void run() = 0;
};

#endif 
