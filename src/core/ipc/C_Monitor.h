#ifndef C_MONITOR_H
#define C_MONITOR_H

#include <pthread.h>

class C_Monitor {
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;

public:
    C_Monitor();
    ~C_Monitor();

    void wait();
    void signal();
    bool timedWait(int seconds);

};

#endif // C_MONITOR_H