#ifndef C_UART_H
#define C_UART_H

#include <string>

#include <cstddef> 
#include <cstdint> 
using namespace std;

class C_UART {

    int m_fd;
    string m_portPath;
public:
    C_UART(int portnumber);
    ~C_UART();
    bool openPort();
    void closePort();
    bool configPort(int baud, int bits, char parity);
    int writeBuffer(const void* data, size_t len);
    int readBuffer(void* buffer, size_t len);
    int getFd() const { return m_fd; }

};

#endif 
