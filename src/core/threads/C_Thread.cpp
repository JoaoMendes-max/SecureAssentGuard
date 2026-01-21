#include "C_Thread.h"
#include <sched.h>      
#include <cstring>      
#include <cerrno>       




C_Thread::C_Thread(int priority) : m_priority(priority) {
    pthread_attr_init(&m_attributes);

    if (m_priority > 0) {
        
        pthread_attr_setschedpolicy(&m_attributes, SCHED_FIFO);

        
        struct sched_param param;
        param.sched_priority = m_priority;
        pthread_attr_setschedparam(&m_attributes, &param);

        
        pthread_attr_setinheritsched(&m_attributes, PTHREAD_EXPLICIT_SCHED);
    }
}




bool C_Thread::start() {

    int result = pthread_create(&m_thread, &m_attributes, internalRun, this );
    
    
    
    
    

    if (result != 0) {
        cerr << "[Erro C_Thread] Falha ao criar thread: " << strerror(result) << endl;
        return false;
    }
    return true;
}


// void* C_Thread::internalRun(void* arg) { C_Thread* threadObj = (C_Thread*)arg; if (threadObj != NULL) { threadObj->run(); } return NULL; }
void* C_Thread::internalRun(void* arg) {
    C_Thread* threadObj = static_cast<C_Thread*>(arg);

    if (threadObj != nullptr) {
        threadObj->run();
    }

    return nullptr;
}




C_Thread::~C_Thread() {
    
    pthread_attr_destroy(&m_attributes);
}

void C_Thread::join() {
    
    pthread_join(m_thread, NULL);
}

void C_Thread::detach() {
    
    pthread_detach(m_thread);
}

void C_Thread::cancel() {
    
    pthread_cancel(m_thread);
}


void C_Thread::requestStop() {
    m_stopRequested.store(true, std::memory_order_relaxed);
}

bool C_Thread::stopRequested() const {
    return m_stopRequested.load(std::memory_order_relaxed);
}
