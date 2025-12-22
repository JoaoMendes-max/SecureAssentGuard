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
    long maxMsgSize;//message nax byte syze
    long maxMsgCount;//max message number inside queue

public:
    //tive de adicionar o createNew por causa do acesso ha queue pelo processo da bse de dados por exemplo

    C_Mqueue(string queueName, long msgSize = 1024, long maxMsgs = 10,bool createNew = true);

    ~C_Mqueue();


    bool send(const void* msg, size_t size, unsigned int prio = 0);
    //thread waits for message queue
    ssize_t receive(void* buffer, size_t size);
    //thread waits for a specific time and if there is no message just keeps going
    ssize_t timedReceive(void* buffer, size_t size, int timeout_sec);
    void unregister();
};

#endif // C_MQUEUE_H