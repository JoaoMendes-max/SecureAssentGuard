#include "C_Mqueue.h"
#include <iostream>
#include <cstring>      
#include <cerrno>       
#include <sys/stat.h>   

// C_Mqueue::C_Mqueue(string queueName, long msgSize, long maxMsgs, bool createNew) { name = queueName; maxMsgSize = msgSize; maxMsgCount = maxMsgs; if (createNew) { struct mq_attr attr; attr.mq_flags = 0; attr.mq_maxmsg = maxMsgs; attr.mq_msgsize = msgSize; attr.mq_curmsgs = 0; id = mq_open(name.c_str(), O_RDWR | O_CREAT, 0666, &attr); } else { id = mq_open(name.c_str(), O_RDWR); } if (id == (mqd_t)-1) { cerr << \"[Erro C_Mqueue] mq_open failed: \" << strerror(errno) << endl; } }
C_Mqueue::C_Mqueue(const string& queueName, long msgSize, long maxMsgs, bool createNew)
    : id(static_cast<mqd_t>(-1)),
      name(queueName),
      maxMsgSize(msgSize),
      maxMsgCount(maxMsgs),
      m_owner(false),
      m_unlinkOnClose(false) {
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = maxMsgs;
    attr.mq_msgsize = msgSize;
    attr.mq_curmsgs = 0;

    if (createNew) {
        id = mq_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666, &attr);
        if (id != static_cast<mqd_t>(-1)) {
            m_owner = true;
        } else if (errno == EEXIST) {
            id = mq_open(name.c_str(), O_RDWR);
        }
    } else {
        id = mq_open(name.c_str(), O_RDWR);
    }

    if (id == static_cast<mqd_t>(-1)) {
        cerr << "[Erro C_Mqueue] mq_open failed: " << strerror(errno) << endl;
        return;
    }

    struct mq_attr actual{};
    if (mq_getattr(id, &actual) == 0) {
        maxMsgSize = actual.mq_msgsize;
        maxMsgCount = actual.mq_maxmsg;
    }
}

C_Mqueue::~C_Mqueue() {
    if (id != (mqd_t)-1) {
        mq_close(id);
        if (m_owner && m_unlinkOnClose) {
            mq_unlink(name.c_str());
        }
    }
}
bool C_Mqueue::send(const void* msg, size_t size, unsigned int prio) {
    if (id == static_cast<mqd_t>(-1)) return false;

    if (size > maxMsgSize) {
        cerr << " [Erro C_Mqueue]Mensagem demasiado grande (" << endl;
        return false;
    }
    if (mq_send(id, reinterpret_cast<const char*>(msg), size, prio) == -1) {
        cerr << "[Erro C_Mqueue] Falha no send: " << strerror(errno) << endl;
        return false;
    }
    return true;
}



ssize_t C_Mqueue::receive(void* buffer, size_t size) {
    if (id == static_cast<mqd_t>(-1)) return -1;

    if (size < this->maxMsgSize) {
        cerr << "[Erro C_Mqueue] Buffer pequeno demais! Precisa de "
             << this->maxMsgSize << " bytes." << endl;
        return -1;
    }

    ssize_t bytes = mq_receive(id, reinterpret_cast<char*>(buffer), size, NULL);

    if (bytes == -1) {
        cerr << "[Erro C_Mqueue] Falha no receive: " << strerror(errno) << endl;
    }
    return bytes;
}

ssize_t C_Mqueue::timedReceive(void* buffer, size_t size, int timeout_sec) {
    if (id == static_cast<mqd_t>(-1)) return -1;

    if (size < maxMsgSize) {
         cerr << "[Erro C_Mqueue] Buffer pequeno demais!" << endl;
         return -1;
    }

    
    struct timespec tm;
    clock_gettime(CLOCK_REALTIME, &tm); 
    tm.tv_sec += timeout_sec;           
    
    ssize_t bytes = mq_timedreceive(id, reinterpret_cast<char*>(buffer), size, NULL, &tm);

    return bytes;
}


void C_Mqueue::unregister() {
    if (m_owner) {
        // Sinaliza para o destrutor que deve remover a fila
        m_unlinkOnClose = true;
    }
}
bool C_Mqueue::isOwner() const {
    return m_owner;
}
