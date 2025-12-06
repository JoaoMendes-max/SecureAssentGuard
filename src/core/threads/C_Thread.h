#ifndef C_THREAD_H
#define C_THREAD_H

#include <pthread.h>
#include <iostream>

using namespace std;

class C_Thread {

    pthread_t m_thread;
    pthread_attr_t m_attributes;
    int m_priority;

    //metodo usado para chamarmos o proprio run de cada classe filha-> vai ser passado como argumento ao pthread_create
    static void* internalRun(void* arg);
    // a funçao q pthread recebe retorna um apontador para void e receb um apointador para void
    //é por isso q esta assim

public:

    C_Thread(int priority = 0); //so vai iniciliazar atribuitos
    virtual ~C_Thread();

    bool start();       // cria thread(pthread creat->q tbm inicia chama o run)
    void join();        // Espera que a thread termine( era aquele exemplo q vi da main)
    void detach();      // Marca a thread para limpeza automática. se ela terminar limpa os recursos automaticamente
    void cancel();      // Termina a thread forçadamente

    virtual void run() = 0;
};

#endif // C_THREAD_H