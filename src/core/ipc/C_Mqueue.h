#ifndef C_MQUEUE_H
#define C_MQUEUE_H

#include <mqueue.h>
#include <string>
#include <ctime>   // Para timedReceive
#include <fcntl.h> // Para O_RDWR, O_CREAT

using namespace std;

class C_Mqueue {
private:
    mqd_t id;
    string name;
    long maxMsgSize;//maximo de bytes da mensagem
    long maxMsgCount;// numero maximo de mensagens q a fila pode ter

public:
    C_Mqueue(string queueName, long msgSize = 1024, long maxMsgs = 10);

    // Destrutor: Fecha a ligação
    ~C_Mqueue();

    // Enviar mensagem
    // Retorna false se a mensagem for maior que maxMsgSize
    bool send(const void* msg, size_t size, unsigned int prio = 0);

    // Receber mensagem
    // Retorna -1 se o buffer for menor que maxMsgSize
    ssize_t receive(void* buffer, size_t size);

    // Receber com timeout (para não bloquear para sempre)
    ssize_t timedReceive(void* buffer, size_t size, int timeout_sec);

    // Apagar a fila do sistema (Usar apenas no fim do programa ou para limpeza)
    void unregister();
};

#endif // C_MQUEUE_H