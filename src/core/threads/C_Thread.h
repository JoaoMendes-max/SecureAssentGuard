#ifndef C_THREAD_H
#define C_THREAD_H

#include <pthread.h>
#include <atomic>
#include <iostream>

using namespace std;

class C_Thread {

    // --- Private Data ---
    pthread_t m_thread;           // Stores the Thread ID.
    pthread_attr_t m_attributes;  // Stores settings like priority.
    int m_priority;               // Real-Time priority value.
    std::atomic<bool> m_stopRequested{false};

    // Bridge function required to connect C++ objects to C-style pthreads.
    static void* internalRun(void* arg);

public:

    // Constructor : fill the attributes(priority,...)
    C_Thread(int priority = 0);

    // Destructor: Cleans up the attributes.
    virtual ~C_Thread();


    // Calls 'pthread_create'(threadid, attributs and run function)
    bool start();

    // Wrapper: Calls 'pthread_join'
    void join();

    // Wrapper: Calls 'pthread_detach'
    void detach();

    // Wrapper: Calls 'pthread_cancel'
    void cancel();

    // Request thread to stop (cooperative shutdown)
    void requestStop();

    // Check if stop was requested
    bool stopRequested() const;

    // Pure Virtual Function. Derived classes must implement their own run!!!
    virtual void run() = 0;
};

#endif // C_THREAD_H
