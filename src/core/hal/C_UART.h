#ifndef C_UART_H
#define C_UART_H

#include <string>

#include <cstddef> // Para size_t
#include <cstdint> // Para tipos inteiros

class C_UART {

    int m_fd;               // File Descriptor (ID do ficheiro aberto)
    std::string m_portPath; // ex: "/dev/ttyAMA2"
public:
    // Construtor (Corrigido para ter o mesmo nome da classe)
    C_UART(int portnumber);

    // Destrutor
    ~C_UART();

    // Abre o ficheiro /dev/...
    bool openPort();

    // Fecha o ficheiro
    void closePort();

    // Configura Baudrate, Bits e Paridade
    // baud: 9600, 115200, etc.
    // bits: 7 ou 8
    // parity: 'N' (None), 'E' (Even), 'O' (Odd)
    //nao esquecer q basicamente vamos usar termios q é uma biblioteca q usa
    bool configPort(int baud, int bits, char parity);

    // Escrever dados (genérico)
    int writeBuffer(const void* data, size_t len);

    // Ler dados (genérico)
    int readBuffer(void* buffer, size_t len);


};

#endif // C_UART_H
