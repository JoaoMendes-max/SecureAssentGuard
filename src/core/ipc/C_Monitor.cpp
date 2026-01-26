/*
 * Monitor implementation with timed wait based on CLOCK_MONOTONIC.
 */

#include "C_Monitor.h"
#include <iostream>
#include <time.h>
#include <errno.h>
using namespace std;

C_Monitor::C_Monitor() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0){
        cerr << "Mutex init failed" << endl;
    }
    
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    if (pthread_cond_init(&m_cond, &attr) != 0) {
        cerr << "Cond init failed" << endl;
    }

    pthread_condattr_destroy(&attr);
}

C_Monitor::~C_Monitor() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

void C_Monitor::wait() {

    // Block indefinitely until signal().
    pthread_mutex_lock(&m_mutex);
    pthread_cond_wait(&m_cond, &m_mutex);
    pthread_mutex_unlock(&m_mutex);
}


void C_Monitor::signal() {
    // Wake all waiting threads.
    pthread_mutex_lock(&m_mutex);
    pthread_cond_broadcast(&m_cond);
    pthread_mutex_unlock(&m_mutex);
}

bool C_Monitor::timedWait(int seconds) {
    // Wait with relative timeout (monotonic).
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += seconds;

    pthread_mutex_lock(&m_mutex);
    int result = pthread_cond_timedwait(&m_cond, &m_mutex, &ts);
    pthread_mutex_unlock(&m_mutex);

    if (result == 0) {
        return false;
    }
    if (result == ETIMEDOUT) {
        return true;
    }
    std::cerr << "[C_Monitor] timedWait error: " << result << std::endl;
    return true;
}
