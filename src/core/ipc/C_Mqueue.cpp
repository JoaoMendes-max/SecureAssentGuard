#include "C_Mqueue.h"
#include <iostream>
#include <cstring>      // Para strerror
#include <cerrno>       // Para errno
#include <sys/stat.h>   // Para permissões

C_Mqueue::C_Mqueue(string queueName, long msgSize, long maxMsgs) {
    name = queueName;
    maxMsgSize = msgSize;   // Guardar para validar no send/receive
    maxMsgCount = maxMsgs;


    struct mq_attr attr;//struct q tem os atributos da mqueu
    attr.mq_flags = 0;           // 0 = Bloqueante
    attr.mq_maxmsg = maxMsgs;    // Capacidade da fila
    attr.mq_msgsize = msgSize;   // Tamanho de cada mensagem
    attr.mq_curmsgs = 0;

    id = mq_open(name.c_str(), O_RDWR | O_CREAT, 0666, &attr);
    if (id == (mqd_t)-1) {//é preciso casting porque o retorn da mq_open é desse tipo
        cerr << "[Erro C_Mqueue] mq_open failed" << endl;
    }
}

C_Mqueue::~C_Mqueue() {
    if (id != (mqd_t)-1) {
        mq_close(id);
    }
}

bool C_Mqueue::send(const void* msg, size_t size, unsigned int prio) {
    if (id == (mqd_t)-1) return false;

    if (size >maxMsgSize) {
        cerr << " [Erro C_Mqueue]Mensagem demasiado grande (" << endl;
        return false;
    }
    if (mq_send(id, (const char*)msg, size, prio) == -1) {
        cerr << "[Erro C_Mqueue] Falha no send: " << strerror(errno) << endl;
        return false;
    }
    return true;
}

ssize_t C_Mqueue::receive(void* buffer, size_t size) {
    if (id == (mqd_t)-1) return -1;

    if (size < this->maxMsgSize) {
        cerr << "[Erro C_Mqueue] Buffer pequeno demais! Precisa de "
             << this->maxMsgSize << " bytes." << endl;
        return -1;
    }

    ssize_t bytes = mq_receive(id, (char*)buffer, size, NULL);

    if (bytes == -1) {
        cerr << "[Erro C_Mqueue] Falha no receive: " << strerror(errno) << endl;
    }
    return bytes;
}

ssize_t C_Mqueue::timedReceive(void* buffer, size_t size, int timeout_sec) {
    if (id == (mqd_t)-1) return -1;

    if (size < maxMsgSize) {
         cerr << "[Erro C_Mqueue] Buffer pequeno demais!" << endl;
         return -1;
    }

    //para controlar o tempo
    struct timespec tm;
    clock_gettime(CLOCK_REALTIME, &tm); // Tempo atual
    tm.tv_sec += timeout_sec;           // Adicionar espera
    //basicamente see chegar ao tempo final e ainda n tiver chegafo nada da return -1
    ssize_t bytes = mq_timedreceive(id, (char*)buffer, size, NULL, &tm);

    return bytes;
}

void C_Mqueue::unregister() {
    // Apaga a fila do sistema
    mq_unlink(this->name.c_str());
}