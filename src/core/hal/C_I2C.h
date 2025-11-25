#ifndef C_I2C_H
#define C_I2C_H

#include <string>
#include <cstdint> // Para uint8_t
#include <cstddef> // Para size_t

class C_I2C {
    int m_bus;          // Número do barramento (1 para RPi)
    uint8_t m_address;  // Endereço I2C do sensor
    int m_fd;           // File Descriptor (O ID do ficheiro aberto no Linux)
    std::string m_devicePath; // Caminho, ex: "/dev/i2c-1"
public:
    // Construtor: Guarda o ID do barramento (ex: 1) e o endereço do sensor (ex: 0x68)
    C_I2C(int bus, uint8_t address);

    // Destrutor: Garante que o ficheiro é fechado
    ~C_I2C();

    // Abre o ficheiro e configura o endereço do escravo (Slave)
    bool init();

    // Fecha a ligação manualmente
    void closeI2C();

    // Escrever um valor num registo (Configuração)
    // Ex: "Mete o valor 0x00 na gaveta 0x6B"
    bool writeRegister(uint8_t reg, uint8_t value);

    // Ler um valor de um registo (Dados Simples)
    // Ex: "O que está na gaveta 0x3B?"
    uint8_t readRegister(uint8_t reg);

    // Ler vários bytes seguidos (Dados em Bloco)
    // Ex: "Lê X, Y e Z de uma vez"
    bool readBytes(uint8_t reg, uint8_t* buffer, size_t len);

};

#endif // C_I2C_H