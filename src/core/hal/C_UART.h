#ifndef C_UART_H
#define C_UART_H

#include <string>
#include <cstddef>

class C_UART
{
public:
    // --- Construtor / Destrutor ---
    explicit C_UART(int portnumber);
    ~C_UART();

    // --- Métodos principais ---
    bool openPort();
    void closePort();
    bool configPort(int baud, int bits, char parity);

    int writeBuffer(const void* data, size_t len);
    int readBuffer(void* buffer, size_t len);

private:
    int m_fd;              // File descriptor da porta serial
    std::string m_portPath; // Caminho ex: "/dev/ttyAMA0"
};

#endif // C_UART_H
