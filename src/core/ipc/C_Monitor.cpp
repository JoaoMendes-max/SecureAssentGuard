#include "C_Monitor.h"
#include <iostream> // Para logs de erro
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