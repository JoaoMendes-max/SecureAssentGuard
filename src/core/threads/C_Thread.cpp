#include "C_Thread.h"
#include <sched.h>      // Para escalonamento de prioridades
#include <cstring>      // Para strerror
#include <cerrno>       // Para errno

// ----------------------------------------------------
// AÇÃO 1: CONSTRUTOR - Configura Prioridades RT
// ----------------------------------------------------
C_Thread::C_Thread(int priority) : m_priority(priority) {
    pthread_attr_init(&m_attributes);

    if (m_priority > 0) {
        // Configurar para política de escalonamento FIFO (Tempo Real)
        pthread_attr_setschedpolicy(&m_attributes, SCHED_FIFO);

        // Atribuir a prioridade
        struct sched_param param;
        param.sched_priority = m_priority;
        pthread_attr_setschedparam(&m_attributes, &param);

        // Usar os atributos definidos explicitamente
        pthread_attr_setinheritsched(&m_attributes, PTHREAD_EXPLICIT_SCHED);
    }
}

// ----------------------------------------------------
// AÇÃO 2: START - Lançar o Processo C++ (Ignição)
// ----------------------------------------------------
bool C_Thread::start() {

    int result = pthread_create(&m_thread, &m_attributes, internalRun, this);
    // o this é o argumento q é passado para a funçao do internalRun
    //por exemplo
    //chamamos funçao ctAct.start(). o this vai ser um apontador para o obejto do tipo ctAct
    //e é isto q dps vai permitir chamar o run certo

    if (result != 0) {
        cerr << "[Erro C_Thread] Falha ao criar thread: " << strerror(result) << endl;
        return false;
    }
    return true;
}


void* C_Thread::internalRun(void* arg) {
    //fazer o casting para chamar o run especifico
    C_Thread* threadObj = (C_Thread*)arg;

    if (threadObj != NULL) {
        threadObj->run();
    }

    return NULL;
}

// ----------------------------------------------------
// AÇÃO 4: GESTÃO E LIMPEZA
// ----------------------------------------------------
C_Thread::~C_Thread() {
    // RAII: Destrói os atributos para libertar recursos do sistema.
    pthread_attr_destroy(&m_attributes);
}

void C_Thread::join() {
    // Bloqueia a thread chamadora até que a thread alvo termine.
    pthread_join(m_thread, NULL);
}

void C_Thread::detach() {
    // Diz ao sistema para limpar os recursos da thread automaticamente quando ela terminar.
    pthread_detach(m_thread);
}

void C_Thread::cancel() {
    // Envia um pedido de terminação forçada. (Usar com extrema cautela!)
    pthread_cancel(m_thread);
}