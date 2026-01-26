#ifndef C_MQUEUE_H
#define C_MQUEUE_H

/*
 * Simple wrapper for POSIX message queues.
 * Used for IPC between daemons and core threads.
 */

#include <mqueue.h>
#include <string>
#include <ctime>   
#include <fcntl.h> 

using namespace std;

class C_Mqueue {
private:
    mqd_t id;
    string name;
    long maxMsgSize;
    long maxMsgCount;
    bool m_owner;
    bool m_unlinkOnClose;

public:
    
    // createNew=true creates and tries to take queue ownership.
    C_Mqueue(const string& queueName, long msgSize = 1024, long maxMsgs = 10, bool createNew = true);
    ~C_Mqueue();
    bool send(const void* msg, size_t size, unsigned int prio = 0);
    ssize_t receive(void* buffer, size_t size);
    ssize_t timedReceive(void* buffer, size_t size, int timeout_sec);
    void unregister();
    bool isOwner() const;
};

#endif 
