#include "C_Monitor.h"
#include <iostream> // Para logs de erro
#include <time.h>
using namespace std;

C_Monitor::C_Monitor() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0){
        cerr << "Mutex init failed" << endl;
    }
    if (pthread_cond_init(&m_cond, NULL) != 0) {
        cerr << "Cond init failed" << endl;
    }
}

C_Monitor::~C_Monitor() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

void C_Monitor::wait() {

    pthread_mutex_lock(&m_mutex);
    pthread_cond_wait(&m_cond, &m_mutex);
    pthread_mutex_unlock(&m_mutex);
}


void C_Monitor::signal() {
    pthread_mutex_lock(&m_mutex);
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
}

bool C_Monitor::timedWait(int seconds) {
    // Calcular tempo absoluto (agora + seconds)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;

    // Esperar com timeout
    pthread_mutex_lock(&m_mutex);
    int result = pthread_cond_timedwait(&m_cond, &m_mutex, &ts);
    pthread_mutex_unlock(&m_mutex);

    // Retornar true se foi timeout, false se acordou por signal
    return (result == ETIMEDOUT);
}
